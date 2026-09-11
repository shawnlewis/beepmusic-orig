#include <stdlib.h>
#include <unistd.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "beep/beep_stream_ch.h"
#include "beep/debug.h"

// A reference to the stack, we must leave the stack with the same number of values
// as it had when we found it, if we're modifying it outside of a standard function
// call.
static lua_State *state;

static int artificial_delay_ms = 0;

static int stream_start(lua_State *L) {
    BeepStreamChCtx *ctx = lua_touserdata(L, 1);
    LOG_DEBUG(log_beep_main, "ctx: %p", ctx);

    lua_pushboolean(L, stream_ch_start(ctx, NULL));

    return 1;
}

static int stream_track_begin(lua_State *L) {
    BeepStreamChCtx *ctx = lua_touserdata(L, 1);
    uint32_t audio_type = lua_tointeger(L, 2);
    LOG_DEBUG(log_beep_main, "ctx: %p", ctx);

    lua_pushboolean(
            L,
            stream_ch_st_begin(
                ctx,
                (uint32_t) audio_type,
                0,
                0,
                0,
                2, // 200ms output threshold, playnet only starts decoding
                   // when it has 200ms of audio.
                0,
                0));

    return 1;
}

static int stream_track_end(lua_State *L) {
    BeepStreamChCtx *ctx = lua_touserdata(L, 1);
    LOG_DEBUG(log_beep_main, "ctx: %p", ctx);

    lua_pushboolean(L, stream_ch_st_end(ctx));

    return 1;
}

static int stream_buffer(lua_State *L) {
    const char* data;
    size_t data_len;

    BeepStreamChCtx *ctx = lua_touserdata(L, 1);

    data = lua_tostring(L, 2);
    if (!data) {
        lua_pushstring(L, "Second argument must be string data");
        lua_error(L);
        return 0;
    }

    data_len = lua_objlen(L, 2);

    lua_pop(L, 1);

    if (artificial_delay_ms) {
        usleep(artificial_delay_ms * 1000);
    }

    lua_pushboolean(L, stream_ch_buffer(ctx, (uint8_t*) data, data_len));

    return 1;
}

static int stream_flush(lua_State *L) {
    BeepStreamChCtx *ctx = lua_touserdata(L, 1);
    int set_cookie = lua_tointeger(L, 2);

    lua_pushboolean(L, stream_ch_flush(ctx, set_cookie));

    return 1;
}

static int stream_close(lua_State *L)
{
    BeepStreamChCtx *ctx = lua_touserdata(L, 1);
    stream_ch_disconnect(ctx);

    return 1;
}

static int stream_free(lua_State *L)
{
    stream_close(L);

    return 1;
}

static void stream_on_connected(bool success, void *priv) {
    int callback_ref = *((int*) priv);
    free(priv);

    lua_rawgeti(state, LUA_REGISTRYINDEX, callback_ref);

    lua_pushboolean(state, success);
    lua_call(state, 1, 0);

    luaL_unref(state, LUA_REGISTRYINDEX, callback_ref);
}

static const luaL_Reg stream_m[] = {
    { "start", stream_start },
    { "track_begin", stream_track_begin },
    { "track_end", stream_track_end },
    { "buffer", stream_buffer },
    { "flush", stream_flush },
    { "close", stream_close },
    { NULL, NULL }
};

static int stream_new(lua_State *L) {
    const char* ip;
    int port;

    ip = lua_tostring(L, 1);
    if (!ip) {
        lua_pushstring(L, "First argument must be string ip address");
        lua_error(L);
        return 0;
    }

    if (!lua_isnumber(L, 2)) {
        lua_pushstring(L, "Second argument must be integer port");
        lua_error(L);
        return 0;
    } else {
        port = lua_tointeger(L, 2);
    }

    if (!lua_isfunction(L, 3)) {
        lua_pushstring(L, "Third argument must be on_connected callback function");
        lua_error(L);
        return 0;
    }

    if (!lua_isnil(L, 4) && lua_isnumber(L, 4)) {
        artificial_delay_ms = lua_tointeger(L, 4);
    }

    // +1
    BeepStreamChCtx *ctx = lua_newuserdata(L, sizeof(BeepStreamChCtx));

    int *callback_ref = malloc(sizeof(int));

    if (!stream_ch_connect(ctx, ip, port, stream_on_connected, callback_ref)) {
        lua_pushnil(L);
        return 1;
    }

    // Store a ref to the connect callback
    lua_pushvalue(L, 3);
    *callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    // Create metatable for ctx and set __index to itself  +1
    lua_createtable(L, 0, 2);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");

    // +0
    lua_pushcfunction(L, stream_free);
    lua_setfield(L, -2, "__gc");

    lua_pushvalue(L, -1);

    // set the table as the metatable of ctx
    lua_setmetatable(L, -3);

    // add methods to instance
    lua_pushvalue(L, -2);
    luaI_openlib(L, NULL, stream_m, 1);

    // Return the instance
    lua_pushvalue(L, -2);

    return 1;
}

static const luaL_Reg stream_reg[] = {
    {"new_stream", stream_new},
    {NULL, NULL},
};

int luaopen_stream(lua_State *L) {
    state = L;

    log_beep_main = LOG_CATEGORY_GET("stream.so");
    log_category_set_priority (log_beep_main, LOG_PRIORITY_DEBUG);

    luaL_openlib (L, "stream", stream_reg, 0);

    return 1;
}
