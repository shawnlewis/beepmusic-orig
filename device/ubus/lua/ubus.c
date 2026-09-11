/*
 * Copyright (C) 2012 Jo-Philipp Wich <jow@openwrt.org>
 * Copyright (C) 2012 John Crispin <blogic@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <unistd.h>
#include <libubus.h>
#include <libubox/blobmsg.h>
#include <libubox/blobmsg_json.h>
#include <lauxlib.h>


#define MODNAME        "ubus"
#define METANAME       MODNAME ".meta"

static lua_State *state;

struct ubus_lua_connection {
	int timeout;
	struct ubus_context *ctx;
};

struct ubus_lua_object {
	struct ubus_object o;
	int r;
};

struct ubus_lua_event_handler {
	struct ubus_event_handler ev;
	int r;
};

struct ubus_lua_call_async {
    struct ubus_context *ctx;
    struct blob_buf *buf;
    const char *func;

    struct ubus_lookup_request lookup_req;
    struct ubus_request req;
    struct uloop_timeout timeout;

    int r;
    struct blob_attr* result;
};

static int
ubus_lua_parse_blob(lua_State *L, struct blob_attr *attr, bool table);

static int
ubus_lua_parse_blob_array(lua_State *L, struct blob_attr *attr, int len, bool table)
{
	int rv;
	int idx = 1;
	int rem = len;
	struct blob_attr *pos;

	lua_newtable(L);

	__blob_for_each_attr(pos, attr, rem)
	{
		rv = ubus_lua_parse_blob(L, pos, table);

		if (rv > 1)
			lua_rawset(L, -3);
		else if (rv > 0)
			lua_rawseti(L, -2, idx++);
	}

	return 1;
}

static int
ubus_lua_parse_blob(lua_State *L, struct blob_attr *attr, bool table)
{
	int len;
	int off = 0;
	void *data;

	if (!blobmsg_check_attr(attr, false))
		return 0;

	if (table && blobmsg_name(attr)[0])
	{
		lua_pushstring(L, blobmsg_name(attr));
		off++;
	}

	data = blobmsg_data(attr);
	len = blobmsg_data_len(attr);

	switch (blob_id(attr))
	{
	case BLOBMSG_TYPE_BOOL:
		lua_pushboolean(L, *(uint8_t *)data);
		break;

	case BLOBMSG_TYPE_INT16:
		lua_pushinteger(L, be16_to_cpu(*(uint16_t *)data));
		break;

	case BLOBMSG_TYPE_INT32:
		lua_pushinteger(L, be32_to_cpu(*(uint32_t *)data));
		break;

	case BLOBMSG_TYPE_INT64:
		lua_pushnumber(L, (double) be64_to_cpu(*(uint64_t *)data));
		break;

	case BLOBMSG_TYPE_STRING:
		// len includes NULL terminator, but lua wants strlen so -1
		lua_pushlstring(L, data, len-1);
		break;

	case BLOBMSG_TYPE_ARRAY:
		ubus_lua_parse_blob_array(L, data, len, false);
		break;

	case BLOBMSG_TYPE_TABLE:
		ubus_lua_parse_blob_array(L, data, len, true);
		break;

	default:
		lua_pushnil(L);
		break;
	}

	return off + 1;
}


static bool
ubus_lua_format_blob_is_array(lua_State *L)
{
	lua_Integer prv = 0;
	lua_Integer cur = 0;

	/* Find out whether table is array-like */
	for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1))
	{
#ifdef LUA_TINT
		if (lua_type(L, -2) != LUA_TNUMBER && lua_type(L, -2) != LUA_TINT)
#else
		if (lua_type(L, -2) != LUA_TNUMBER)
#endif
		{
			lua_pop(L, 2);
			return false;
		}

		cur = lua_tointeger(L, -2);

		if ((cur - 1) != prv)
		{
			lua_pop(L, 2);
			return false;
		}

		prv = cur;
	}

	return true;
}

static int
ubus_lua_format_blob_array(lua_State *L, struct blob_buf *b, bool table);

static int
ubus_lua_format_blob(lua_State *L, struct blob_buf *b, bool table)
{
	void *c;
	bool rv = true;
	const char *key = table ? lua_tostring(L, -2) : NULL;

	switch (lua_type(L, -1))
	{
	case LUA_TBOOLEAN:
		blobmsg_add_u8(b, key, (uint8_t)lua_toboolean(L, -1));
		break;

#ifdef LUA_TINT
	case LUA_TINT:
#endif
	case LUA_TNUMBER:
		blobmsg_add_u32(b, key, (uint32_t)lua_tointeger(L, -1));
		break;

	case LUA_TSTRING:
	case LUA_TUSERDATA:
	case LUA_TLIGHTUSERDATA: {
		const char* str;
		size_t str_len;
		str = lua_tolstring(L, -1, &str_len);
		// blob.c:blob_check_type requires NULL terminator, so include it
		// with str_len + 1
		blobmsg_add_field(b, BLOBMSG_TYPE_STRING, key, str, str_len + 1);
		break;
	}

	case LUA_TTABLE:
		if (ubus_lua_format_blob_is_array(L))
		{
			c = blobmsg_open_array(b, key);
			rv = ubus_lua_format_blob_array(L, b, false);
			blobmsg_close_array(b, c);
		}
		else
		{
			c = blobmsg_open_table(b, key);
			rv = ubus_lua_format_blob_array(L, b, true);
			blobmsg_close_table(b, c);
		}
		break;

	default:
		rv = false;
		break;
	}

	return rv;
}

static int
ubus_lua_format_blob_array(lua_State *L, struct blob_buf *b, bool table)
{
	for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1))
	{
		if (!ubus_lua_format_blob(L, b, table))
		{
			lua_pop(L, 1);
			return false;
		}
	}

	return true;
}


static int
ubus_lua_connect(lua_State *L)
{
	struct ubus_lua_connection *c;
	const char *sockpath = luaL_optstring(L, 1, NULL);
	int timeout = luaL_optint(L, 2, 30);

	if ((c = lua_newuserdata(L, sizeof(*c))) != NULL &&
		(c->ctx = ubus_connect(sockpath)) != NULL)
	{
		ubus_add_uloop(c->ctx);
		c->timeout = timeout;
		luaL_getmetatable(L, METANAME);
		lua_setmetatable(L, -2);
		return 1;
	}

	/* NB: no errors from ubus_connect() yet */
	lua_pushnil(L);
	lua_pushinteger(L, UBUS_STATUS_UNKNOWN_ERROR);
	return 2;
}


static void
ubus_lua_objects_cb(struct ubus_context *c, struct ubus_object_data *o, void *p)
{
	lua_State *L = (lua_State *)p;

	lua_pushstring(L, o->path);
	lua_rawseti(L, -2, lua_objlen(L, -2) + 1);
}

static int
ubus_lua_objects(lua_State *L)
{
	int rv;
	struct ubus_lua_connection *c = luaL_checkudata(L, 1, METANAME);

	lua_newtable(L);
	rv = ubus_lookup(c->ctx, NULL, ubus_lua_objects_cb, L);

	if (rv != UBUS_STATUS_OK)
	{
		lua_pop(L, 1);
		lua_pushnil(L);
		lua_pushinteger(L, rv);
		return 2;
	}

	return 1;
}

static int
ubus_method_handler(struct ubus_context *ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *method,
		struct blob_attr *msg)
{
	struct ubus_lua_object *o = container_of(obj, struct ubus_lua_object, o);

	lua_getglobal(state, "__ubus_cb"); // +1
	lua_rawgeti(state, -1, o->r); // +1
	lua_getfield(state, -1, method); // +1

	if (lua_isfunction(state, -1)) { // 0
		lua_pushlightuserdata(state, req); // +1
		if (!msg) { // +1
			lua_pushnil(state);
        } else {
			ubus_lua_parse_blob_array(state, blob_data(msg), blob_len(msg), true);
        }

		lua_call(state, 2, 0); // -3
	    lua_pop(state, 2);
	} else {
        fprintf(stderr, "Retrieved nil/corrupted cb from __ubus_cb\n");
        abort();
    }

	return 0;
}

static int lua_gettablelen(lua_State *L, int index)
{
	int cnt = 0;

	lua_pushnil(L);
	index -= 1;
	while (lua_next(L, index) != 0) {
		cnt++;
		lua_pop(L, 1);
	}

	return cnt;
}

static int ubus_lua_reply(lua_State *L)
{
	int ret = 2;
	struct ubus_lua_connection *c = luaL_checkudata(L, 1, METANAME);
	struct ubus_request_data *req;
	struct blob_buf buf;

	luaL_checktype(L, 3, LUA_TTABLE);

	memset(&buf, 0, sizeof(buf));
	blob_buf_init(&buf, 0);

	if (!ubus_lua_format_blob_array(L, &buf, true))
	{
        fprintf(stderr, "OF COURSE I'M NOT HERE\n");
		lua_pushnil(L);
		lua_pushinteger(L, UBUS_STATUS_INVALID_ARGUMENT);
		goto out;
	}

	req = lua_touserdata(L, 2);
	ubus_send_reply(c->ctx, req, buf.head);

	ret = 0;

out:
	blob_buf_free(&buf);
	return ret;
}

static int ubus_lua_defer(lua_State *L)
{
	struct ubus_lua_connection *c = luaL_checkudata(L, 1, METANAME);
	struct ubus_request_data *req;
	struct ubus_request_data *new_req = malloc(sizeof(struct ubus_request_data));

	req = lua_touserdata(L, 2);
	lua_pushlightuserdata(L, new_req);

	ubus_defer_request(c->ctx, req, new_req);

	return 1;
}

static int ubus_lua_complete_deferred(lua_State *L)
{
	struct ubus_lua_connection *c = luaL_checkudata(L, 1, METANAME);
	struct ubus_request_data *req = lua_touserdata(L, 2);
	int ret = lua_tointeger(L, 3);

	ubus_complete_deferred_request(c->ctx, req, ret);

	free(req);

	return 0;
}

static int ubus_lua_load_methods(lua_State *L, struct ubus_method *m)
{
	struct blobmsg_policy *p;
	int plen;
	int pidx = 0;

	/* get the function pointer */
	lua_pushinteger(L, 1);
	lua_gettable(L, -2);

	/* get the policy table */
	lua_pushinteger(L, 2);
	lua_gettable(L, -3);
	plen = lua_gettablelen(L, -1);

	/* check if the method table is valid */
	if ((lua_type(L, -2) != LUA_TFUNCTION) ||
			(lua_type(L, -1) != LUA_TTABLE) ||
			lua_objlen(L, -1) || !plen) {
		lua_pop(L, 2);
		return 1;
	}

	/* store function pointer */
	lua_pushvalue(L, -2);
	lua_setfield(L, -6, lua_tostring(L, -5));

	/* setup the policy pointers */
	p = malloc(sizeof(struct blobmsg_policy) * plen);
	memset(p, 0, sizeof(struct blobmsg_policy) * plen);
	m->policy = p;
	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		int val = lua_tointeger(L, -1);

		/* check if the policy is valid */
		if ((lua_type(L, -2) != LUA_TSTRING) ||
				(lua_type(L, -1) != LUA_TNUMBER) ||
				(val < 0) ||
				(val > BLOBMSG_TYPE_LAST)) {
			lua_pop(L, 1);
			continue;
		}
		p[pidx].name = lua_tostring(L, -2);
		p[pidx].type = val;
		lua_pop(L, 1);
		pidx++;
	}

	m->n_policy = pidx;
	m->name = lua_tostring(L, -4);
	m->handler = ubus_method_handler;
	lua_pop(L, 2);

	return 0;
}

static struct ubus_object* ubus_lua_load_object(lua_State *L)
{
	struct ubus_lua_object *obj = NULL;
	int mlen = lua_gettablelen(L, -1);
	struct ubus_method *m;
	int midx = 0;

	/* setup object pointers */
	obj = malloc(sizeof(struct ubus_lua_object));
	memset(obj, 0, sizeof(struct ubus_lua_object));
	obj->o.name = lua_tostring(L, -2);

	/* setup method pointers */
	m = malloc(sizeof(struct ubus_method) * mlen);
	memset(m, 0, sizeof(struct ubus_method) * mlen);
	obj->o.methods = m;

	/* setup type pointers */
	obj->o.type = malloc(sizeof(struct ubus_object_type));
	memset(obj->o.type, 0, sizeof(struct ubus_object_type));
	obj->o.type->name = lua_tostring(L, -2);
	obj->o.type->id = 0;
	obj->o.type->methods = obj->o.methods;

	/* create the he callback lookup table */
	lua_createtable(L, 1, 0);
	lua_getglobal(L, "__ubus_cb");
	lua_pushvalue(L, -2);
	obj->r = luaL_ref(L, -2);
	lua_pop(L, 1);

	/* scan each method */
	lua_pushnil(L);
	while (lua_next(L, -3) != 0) {
		/* check if it looks like a method */
		if ((lua_type(L, -2) != LUA_TSTRING) ||
				(lua_type(L, -1) != LUA_TTABLE) ||
				!lua_objlen(L, -1)) {
			lua_pop(L, 1);
			continue;
		}

		if (!ubus_lua_load_methods(L, &m[midx]))
			midx++;
		lua_pop(L, 1);
	}

	obj->o.type->n_methods = obj->o.n_methods = midx;

	/* pop the callback table */
	lua_pop(L, 1);

	return &obj->o;
}

static int ubus_lua_add(lua_State *L)
{
	struct ubus_lua_connection *c = luaL_checkudata(L, 1, METANAME);

	/* verify top level object */
	if (lua_istable(L, 1)) {
		lua_pushstring(L, "you need to pass a table");
		lua_error(L);
		return 0;
	}

	/* scan each object */
	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		struct ubus_object *obj = NULL;

		/* check if the object has a table of methods */
		if ((lua_type(L, -2) == LUA_TSTRING) && (lua_type(L, -1) == LUA_TTABLE)) {
			obj = ubus_lua_load_object(L);

			if (obj)
				ubus_add_object_async(c->ctx, obj, NULL, NULL);
		}
		lua_pop(L, 1);
	}

	return 0;
}

static int ubus_lua_remove(lua_State *L)
{
	struct ubus_lua_connection *c = luaL_checkudata(L, 1, METANAME);

	/* verify top level object */
	if (lua_istable(L, 1)) {
		lua_pushstring(L, "you need to pass a table");
		lua_error(L);
		return 0;
	}

	/* scan each object */
	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		struct ubus_object *obj = NULL;

		/* check if the object has a table of methods */
		if ((lua_type(L, -2) == LUA_TSTRING) && (lua_type(L, -1) == LUA_TTABLE)) {
			obj = ubus_lua_load_object(L);

			if (obj)
				ubus_remove_object(c->ctx, obj);
		}
		lua_pop(L, 1);
	}

	return 0;
}

static int ubus_lua_send_event(lua_State *L)
{
	int ret = 2;
	struct ubus_lua_connection *c = luaL_checkudata(L, 1, METANAME);
	const char *type = luaL_checkstring(L, 2);
	struct blob_buf buf;

	luaL_checktype(L, 3, LUA_TTABLE);

	memset(&buf, 0, sizeof(buf));
	blob_buf_init(&buf, 0);

	if (!ubus_lua_format_blob_array(L, &buf, true))
	{
		lua_pushnil(L);
		lua_pushinteger(L, UBUS_STATUS_INVALID_ARGUMENT);
		goto out;
	}

	ubus_send_event_async(c->ctx, type, buf.head);

	ret = 0;

out:
	blob_buf_free(&buf);
	return ret;;
}

static void ubus_listen_handler(struct ubus_context *ctx, struct ubus_event_handler *ev_obj,
		const char *type, struct blob_attr *msg)
{
	struct ubus_lua_event_handler *lua_ev = container_of(ev_obj, struct ubus_lua_event_handler, ev);

	lua_getglobal(state, "__ubus_cb"); // +1
	lua_rawgeti(state, -1, lua_ev->r); // +1

	if (lua_isfunction(state, -1)) {
		lua_pushstring(state, type); // +1
		if (!msg) {
			lua_pushnil(state);
        } else {
			ubus_lua_parse_blob_array(state, blob_data(msg), blob_len(msg), true);
        }
        lua_call(state, 2, 0);
	    lua_pop(state, 1);
	} else {
        fprintf(stderr, "Retrieved nil/corrupted cb from __ubus_cb\n");
        abort();
    }
}

static void
ubus_lua_listen_async_finish(void* priv) {
	int r = *((int*)priv);
	free(priv);

	lua_getglobal(state, "__ubus_cb"); // +1
	lua_rawgeti(state, -1, r); // +1
	luaL_unref(state, -2, r); // 0

	if (lua_isfunction(state, -1)) {
		lua_call(state, 0, 0); // -1
		lua_pop(state, 1);  // -1
	} else {
		abort();
	}
}

static int ubus_listen(lua_State *L)
{
	struct ubus_lua_event_handler *lua_ev = NULL;
	int ret;

	struct ubus_lua_connection *c = luaL_checkudata(L, 1, METANAME);
	const char *event = luaL_checkstring(L, 2);
	int *register_done_ref = NULL;

	if(!lua_isfunction(L, 3))
	{
		lua_pushnil(L);
		lua_pushinteger(L, UBUS_STATUS_INVALID_ARGUMENT);
		return 2;
	}
	if (lua_isfunction(L, 4)) {
		lua_getglobal(L, "__ubus_cb");
		lua_pushvalue(L, 4);
		register_done_ref = malloc(sizeof(int));
		*register_done_ref = luaL_ref(L, -2);
        lua_pop(L, 1);
	}

	lua_ev = malloc(sizeof(struct ubus_lua_event_handler));
	memset(lua_ev, 0, sizeof(struct ubus_lua_event_handler));

	lua_ev->ev.cb = ubus_listen_handler;

	lua_getglobal(L, "__ubus_cb");
	lua_pushvalue(L, 3);
	lua_ev->r = luaL_ref(L, -2);

	if (register_done_ref) {
		ret = ubus_register_event_handler_async(c->ctx, &lua_ev->ev,
		        event, ubus_lua_listen_async_finish, register_done_ref);
	} else {
		ret = ubus_register_event_handler_async(c->ctx, &lua_ev->ev,
		        event, NULL, NULL);
	}

	if(ret) {
		lua_pushnil(L);
		lua_pushinteger(L, ret);
		return 2;
	}

	lua_pushlightuserdata(L, &lua_ev->ev);
	return 1;
}

static void ubus_cancel_listen_cb(void *priv)
{
    struct ubus_lua_event_handler *lua_ev = (struct ubus_lua_event_handler *)
        priv;

    free(lua_ev);
}

static int ubus_cancel_listen(lua_State *L)
{
	struct ubus_lua_connection *c = luaL_checkudata(L, 1, METANAME);
	struct ubus_event_handler *ev_obj;
	struct ubus_lua_event_handler *lua_ev = NULL;

	int ret;

	if(!(ev_obj = (struct ubus_event_handler *)lua_touserdata(L, 2)))
	{
		lua_pushnil(L);
		lua_pushinteger(L, UBUS_STATUS_INVALID_ARGUMENT);
		return 2;
	}


	lua_ev = container_of(ev_obj, struct ubus_lua_event_handler, ev);
	ret = ubus_unregister_event_handler_async(c->ctx, ev_obj, ubus_cancel_listen_cb, lua_ev);

	if(ret)
	{
		lua_pushnil(L);
		lua_pushinteger(L, ret);
		return 2;
	}

	return 0;
}

static void
ubus_lua_signatures_cb(struct ubus_context *c, struct ubus_object_data *o, void *p)
{
	lua_State *L = (lua_State *)p;

	if (!o->signature)
		return;

	ubus_lua_parse_blob_array(L, blob_data(o->signature), blob_len(o->signature), true);
}

static int
ubus_lua_signatures(lua_State *L)
{
	int rv;
	struct ubus_lua_connection *c = luaL_checkudata(L, 1, METANAME);
	const char *path = luaL_checkstring(L, 2);

	rv = ubus_lookup(c->ctx, path, ubus_lua_signatures_cb, L);

	if (rv != UBUS_STATUS_OK)
	{
		lua_pop(L, 1);
		lua_pushnil(L);
		lua_pushinteger(L, rv);
		return 2;
	}

	return 1;
}


static void
ubus_lua_call_cb(struct ubus_request *req, int type, struct blob_attr *msg)
{

	struct blob_attr** result = (struct blob_attr**) req->priv;
	if (msg) {
		*result = blob_memdup(msg);
	} else {
		*result = NULL;
	}
}

static int
ubus_lua_call(lua_State *L)
{
	int rv, top;
	uint32_t id;
	struct ubus_lua_connection *c = luaL_checkudata(L, 1, METANAME);
	const char *path = luaL_checkstring(L, 2);
	const char *func = luaL_checkstring(L, 3);
	struct blob_buf buf;
	int ret = 2;

	luaL_checktype(L, 4, LUA_TTABLE);
	memset(&buf, 0, sizeof(buf));
	blob_buf_init(&buf, 0);

	if (!ubus_lua_format_blob_array(L, &buf, true))
	{
		lua_pushnil(L);
		lua_pushinteger(L, UBUS_STATUS_INVALID_ARGUMENT);
		goto out;
	}

	rv = ubus_lookup_id(c->ctx, path, &id);

	if (rv)
	{
		lua_pushnil(L);
		lua_pushinteger(L, rv);
		goto out;
	}

	top = lua_gettop(L);
	struct blob_attr* result;
	rv = ubus_invoke(c->ctx, id, func, buf.head, ubus_lua_call_cb, &result, c->timeout * 1000);

	if (rv != UBUS_STATUS_OK)
	{
		lua_pop(L, 1);
		lua_pushnil(L);
		lua_pushinteger(L, rv);
		goto out;
	}

	if (result) {
		ubus_lua_parse_blob_array(L, blob_data(result), blob_len(result), true);
	} else {
		lua_pushnil(L);
	}
	free(result);

	if (lua_gettop(L) - top != 1) {
		abort();
	}
	ret = 1;

out:
	blob_buf_free(&buf);
	return ret;
}

// Called when the call async is done.
static void
ubus_lua_call_async_finish(int ret, struct blob_attr* result,  void* priv) {
    int r = *((int*)priv);
    free(priv);

    lua_getglobal(state, "__ubus_cb"); // +1
    lua_rawgeti(state, -1, r); // +1
    luaL_unref(state, -2, r); // 0

    if (lua_isfunction(state, -1)) {
        if (!result) { // +1
            lua_pushnil(state);
        } else {
            ubus_lua_parse_blob_array(state, blob_data(result),
                    blob_len(result), true);
        }
        if (ret) { // +1
            lua_pushinteger(state, ret);
        } else {
            lua_pushnil(state);
        }
        lua_call(state, 2, 0); // 1 + (n_args) = 3
        lua_pop(state, 1);
    } else {
        abort();
    }
}

// Returns nil on success or a ubus error code (integer) on error.
static int ubus_lua_call_async(lua_State *L) {
    struct ubus_lua_connection *c = luaL_checkudata(L, 1, METANAME);
    const char *path = luaL_checkstring(L, 2);
    const char *func = luaL_checkstring(L, 3);
    struct blob_buf *buf = calloc(1, sizeof(struct blob_buf));

    luaL_checktype(L, 4, LUA_TTABLE);
    blob_buf_init(buf, 0);

    lua_pushvalue(L, 4);
    if (!ubus_lua_format_blob_array(L, buf, true)) {
        lua_pushinteger(L, UBUS_STATUS_INVALID_ARGUMENT);
        goto out;
    }
    lua_pop(L, 1);

    if (!lua_isfunction(L, 5)) {
        lua_pushinteger(L, UBUS_STATUS_INVALID_ARGUMENT);
        goto out;
    }

    int timeout = luaL_optint(L, 6, 0);

    lua_getglobal(L, "__ubus_cb");
    lua_pushvalue(L, 5);
    int *r = malloc(sizeof(int));
    *r = luaL_ref(L, -2);

    ubus_invoke_async_full(c->ctx, path, func, buf->head, timeout,
            ubus_lua_call_async_finish, r);

    // Success! buf and ca will be freed later during the async callbacks
    lua_pushnil(L);

out:
    blob_buf_free(buf);
    free(buf);
    return 1;
}

static int
ubus_lua__gc(lua_State *L)
{
	struct ubus_lua_connection *c = luaL_checkudata(L, 1, METANAME);

	if (c->ctx != NULL)
	{
		ubus_free(c->ctx);
		memset(c, 0, sizeof(*c));
	}

	return 0;
}

static const luaL_Reg ubus[] = {
	{ "connect", ubus_lua_connect },
	{ "objects", ubus_lua_objects },
	{ "add", ubus_lua_add },
	{ "remove", ubus_lua_remove },
	{ "send_event", ubus_lua_send_event },
	{ "listen", ubus_listen },
	{ "cancel_listen", ubus_cancel_listen },
	{ "reply", ubus_lua_reply },
	{ "defer", ubus_lua_defer },
	{ "complete_deferred", ubus_lua_complete_deferred },
	{ "signatures", ubus_lua_signatures },
	{ "call", ubus_lua_call },
	{ "call_async", ubus_lua_call_async },
	{ "close", ubus_lua__gc },
	{ "__gc", ubus_lua__gc },
	{ NULL, NULL },
};

/* avoid missing prototype warning */
int luaopen_ubus(lua_State *L);

extern bool ubus_complete_request_allowed;

int
luaopen_ubus(lua_State *L)
{
	/* create metatable */
	luaL_newmetatable(L, METANAME);

	/* metatable.__index = metatable */
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");

	/* fill metatable */
	luaL_register(L, NULL, ubus);
	lua_pop(L, 1);

	/* create module */
	luaL_register(L, MODNAME, ubus);

	/* set some enum defines */
	lua_pushinteger(L, BLOBMSG_TYPE_ARRAY);
	lua_setfield(L, -2, "ARRAY");
	lua_pushinteger(L, BLOBMSG_TYPE_TABLE);
	lua_setfield(L, -2, "TABLE");
	lua_pushinteger(L, BLOBMSG_TYPE_STRING);
	lua_setfield(L, -2, "STRING");
	lua_pushinteger(L, BLOBMSG_TYPE_INT64);
	lua_setfield(L, -2, "INT64");
	lua_pushinteger(L, BLOBMSG_TYPE_INT32);
	lua_setfield(L, -2, "INT32");
	lua_pushinteger(L, BLOBMSG_TYPE_INT16);
	lua_setfield(L, -2, "INT16");
	lua_pushinteger(L, BLOBMSG_TYPE_INT8);
	lua_setfield(L, -2, "INT8");
	lua_pushinteger(L, BLOBMSG_TYPE_BOOL);
	lua_setfield(L, -2, "BOOLEAN");

	/* used in our callbacks */
	state = L;

	/* create the callback table */
	lua_createtable(L, 1, 0);
	lua_setglobal(L, "__ubus_cb");

	ubus_complete_request_allowed = false;

	return 0;
}
