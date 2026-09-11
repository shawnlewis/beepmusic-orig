#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "beep/debug.h"
#include "beep/beep_curl.h"

#define CURL_REF_TABLE "__curl_refs"

typedef struct event_stream_context {
    int ref_self;
    int ref_url;
    int ref_data_cb;
    int ref_done_cb;
    int ref_redirect_cb;
    BeepEventStream *event_stream;
} event_stream_context;

static lua_State *state;

static int data_cb(BeepEventStream *stream,
        const void *ptr, size_t size, void *priv) {
    int ss_start = lua_gettop(state);
    int ret;
    event_stream_context *context = (event_stream_context *)priv;

    lua_getglobal(state, CURL_REF_TABLE); // +1 curl ref table
    lua_rawgeti(state, -1, context->ref_data_cb); // +1 cb, curl ref table

    lua_pushstring(state, ptr); // +1, data ptr, cb, curl ref table
    lua_pushnumber(state, (double)size); // +1, size, data ptr, cb,
                                         // curl ref table
    lua_call(state, 2, 1); // -3, +1 return value, curl ref table
    ret = luaL_checkint(state, -1); // 0 return value, curl ref table
    lua_pop(state, 2); // -2 (nothing)

    assert(ss_start == lua_gettop(state));
    return ret;
}

static int done_cb(BeepEventStream *stream,
        long code, void *priv) {
    int ss_start = lua_gettop(state);
    int ret;
    event_stream_context *context = (event_stream_context *)priv;

    lua_getglobal(state, CURL_REF_TABLE); // +1 c/r/t
    lua_rawgeti(state, -1, context->ref_done_cb); // +1 cb, c/r/t

    lua_pushnumber(state, (double)code); // +1 code, cb, c/r/t
    lua_call(state, 1, 1); // -2, +1 return value, c/r/t
    ret = luaL_checkint(state, -1); // 0 return value, c/r/t
    lua_pop(state, 1); // -1 c/r/t

    // Remove all references so this object and all associated callbacks
    // are available for GC
    luaL_unref(state, -1, context->ref_self);        // 0 c/r/t for all unref
    luaL_unref(state, -1, context->ref_url);         //   calls here
    luaL_unref(state, -1, context->ref_data_cb);
    luaL_unref(state, -1, context->ref_done_cb);
    luaL_unref(state, -1, context->ref_redirect_cb);

    lua_pop(state, 1); // -1 (nothing)

    assert(ss_start == lua_gettop(state));
    return ret;
}

static int redirect_cb(BeepEventStream *stream,
        const char *url, long code, void *priv) {
    int ss_start = lua_gettop(state);
    int ret;
    event_stream_context *context = (event_stream_context *)priv;

    lua_getglobal(state, CURL_REF_TABLE); // +1 c/r/t
    lua_rawgeti(state, -1, context->ref_redirect_cb); // +1 cb, c/r/t

    lua_pushstring(state, url); // +1 url, cb, c/r/t
    lua_pushnumber(state, (double)code); // +1 code, url, cb, c/r/t
    lua_call(state, 2, 1); // -3, +1 return value, c/r/t
    ret = luaL_checkint(state, -1); // 0 return value, c/r/t
    lua_pop(state, 2); // -2 (nothing)

    assert(ss_start == lua_gettop(state));
    return ret;
}

static int lua_curl_event_stream_close(lua_State *L) {
    int ret;

    event_stream_context *context =
            (event_stream_context *)lua_touserdata(L, lua_upvalueindex(1));

    LOG_DEBUG(log_beep_main, "Closing event_stream_context %p", context);

    ret = beep_curl_event_stream_close(context->event_stream);
    if(ret) {
        lua_pushstring(L, "Failed to close event stream");
        lua_error(L);
    }

    // Cleanup is done by done_cb

    return 0;
}

static int _lua_curl_event_stream_gc(lua_State *L) {
    // N.B.: Should not have to close the event stream at this point.  If the
    // event stream object is out of scope and being GC'd, the done callback
    // has been called and the event stream library has already freed its
    // resources.
    // return lua_curl_event_stream_close(L);
    return 0;
}

static const luaL_Reg event_stream_methods[] = {
    {"close", lua_curl_event_stream_close},
    {NULL, NULL}
};

static int lua_curl_event_stream(lua_State *L) {
    event_stream_context *context;
    const char *url = luaL_checkstring(L, 1);

    // Verify parameters
    if(!url) {
        lua_pushstring(L, "Invalid argument 1: Must be URL");
        lua_error(L);

        return 0;
    }

    if(!lua_isfunction(L, 2)) {
        lua_pushstring(L, "Invalid argument 2: Must be data_cb");
        lua_error(L);

        return 0;
    }

    if(!lua_isfunction(L, 3)) {
        lua_pushstring(L, "Invalid argument 3: Must be done_cb");
        lua_error(L);

        return 0;
    }

    if(!lua_isfunction(L, 4) && !lua_isnil(L, 4)) {
        lua_pushstring(L, "Invalid argument 4: Must be redirect_cb or nil");
        lua_error(L);

        return 0;
    }

    context = lua_newuserdata(L, sizeof(event_stream_context)); // +1 (context)
    if(!context) {
        LOG_ERROR(log_beep_main, "Failed to allocate event_stream_context");
        abort();
    }

    memset(context, 0, sizeof(event_stream_context));

    context->event_stream = beep_curl_event_stream(url,
            data_cb, done_cb, redirect_cb, context);
    if (!context->event_stream) {
        // No references to context retained, lua should gc this automatically
        lua_pop(L, 1); // -1 *empty*
        lua_pushnil(L); // +1 (nil)
        return 1;
    }

    // Add and store references to our reference table
    lua_getglobal(L, CURL_REF_TABLE); // +1 (curl ref table, context)
    lua_pushvalue(L, 1); // +1 (url, curl ref table, context)
    context->ref_url = luaL_ref(L, -2); // -1 (curl ref table, context)

    lua_pushvalue(L, 2); // +1 (data_cb, curl ref table, context)
    context->ref_data_cb = luaL_ref(L, -2); // -1 (curl ref table, context)

    lua_pushvalue(L, 3); // +1 (done_cb, curl ref table, context)
    context->ref_done_cb = luaL_ref(L, -2); // -1 (curl ref table, context)

    if(!lua_isnil(L, 4)) {
        lua_pushvalue(L, 4); // +1 (redirect_cb, curl ref table, context)
        context->ref_redirect_cb =
                luaL_ref(L, -2); // -1 (curl ref table, context)
    }

    // (curl ref table, context)

    lua_pushvalue(L, -2); // +1 (context, c/r/t, context)
    context->ref_self = luaL_ref(L, -2); // -1 (c/r/t, context)

    lua_createtable(L, 0, 2); // +1 (metatable, curl ref table, context)
    lua_pushvalue(L, -1); // +1 (metatable, metatable, curl ref table, context)
    lua_setfield(L, -2, "__index"); // -1 (metatable, curl ref table, context)
    lua_pushvalue(L, -3); // +1 (context, metatable, curl ref table, context)
    lua_pushcclosure(L,
            _lua_curl_event_stream_gc, 1); // -1,+1 (gc func, metatable,
                                           // curl ref table, context)
    lua_setfield(L, -2, "__gc"); // -1 (metatable, curl ref table, context)
    lua_pushvalue(L, -3); // +1 (context, metatable, curl ref table, context)
    luaI_openlib(L, NULL, event_stream_methods, 1);
            // -1 (metatable, curl ref table, context)
    lua_setmetatable(L, -3); // -1 (curl ref table, context)
    lua_pop(L, 1); // -1 (context)

    return 1;
}

static int lua_curl_init(lua_State *L) {
    int ret = beep_curl_init();
    lua_pushboolean(L, !!ret);

    return 1;
}

static int lua_curl_destroy(lua_State *L) {
    int ret = beep_curl_destroy();
    lua_pushboolean(L, !!ret);

    return 1;
}

static luaL_reg curl_func[] = {
    {"init", lua_curl_init},
    {"destroy", lua_curl_destroy},
    //TODO: Expose perform via get/post/put/request methods but hide CURL
    //details
    {"event_stream", lua_curl_event_stream},
    {NULL, NULL}
};

int luaopen_curl(lua_State *L) {
    log_beep_main = LOG_CATEGORY_GET("curl.so");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    state = L;

    lua_createtable(L, 1, 0);
    lua_setglobal(L, CURL_REF_TABLE);

    luaL_openlib(L, "curl", curl_func, 0);

    return 1;
}
