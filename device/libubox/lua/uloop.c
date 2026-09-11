/*
 * Copyright (C) 2012 John Crispin <blogic@openwrt.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "../uloop.h"
#include "../list.h"

struct lua_uloop_timeout {
	struct uloop_timeout t;
	int r;
	int self_ref;
	bool set;
};

struct lua_uloop_process {
	struct uloop_process p;
	int r;
	int self_ref;
};

static lua_State *state;

static void ul_timer_cb(struct uloop_timeout *t)
{
	struct lua_uloop_timeout *tout =
		container_of(t, struct lua_uloop_timeout, t);

	lua_getglobal(state, "__uloop_cb");
	lua_rawgeti(state, -1, tout->r);

	uloop_timeout_cancel(t);

	// tout->self_ref prevents Lua from GCing the timeout object.  We want
	// to maintain it for the duration of the user callback, in case they
	// call :set on the timeout object.  If not, the timeout has expired and
	// we should allow lua to clean it up.
	tout->set = false;

	lua_call(state, 0, 0);
	if(!tout->set) {
		if (tout->r != -1) {
			luaL_unref(state, -1, tout->r);
			luaL_unref(state, -1, tout->self_ref);
			tout->r = -1;
		}
	}

	lua_pop(state, 1);
}

static int ul_timer_set(lua_State *L)
{
	struct lua_uloop_timeout *tout;
	double set;

	if (!lua_isnumber(L, -1)) {
		lua_pushstring(L, "invalid arg list");
		lua_error(L);

		return 0;
	}

	set = lua_tointeger(L, -1);
	tout = lua_touserdata(L, 1);
	uloop_timeout_set(&tout->t, set);
	tout->set = true;

	return 0;
}

static int ul_timer_free(lua_State *L)
{
	struct lua_uloop_timeout *tout = lua_touserdata(L, 1);
	uloop_timeout_cancel(&tout->t);
	lua_getglobal(L, "__uloop_cb");
	if (tout->r != -1) {
		luaL_unref(L, -1, tout->r);
		luaL_unref(L, -1, tout->self_ref);
		tout->r = -1;
	}

	return 0;
}

static const luaL_Reg timer_m[] = {
	{ "set", ul_timer_set },
	{ "cancel", ul_timer_free },
	{ NULL, NULL }
};

static int ul_timer(lua_State *L)
{
	struct lua_uloop_timeout *tout;
	int set = 0;
	int have_set = 0;
	int ref;
	int self_ref;

	if (lua_isnumber(L, -1)) {
		have_set = 1;
		set = lua_tointeger(L, -1);
		lua_pop(L, 1);
	}

	if (!lua_isfunction(L, -1)) {
		lua_pushstring(L, "invalid arg list");
		lua_error(L);

		return 0;
	}

	lua_getglobal(L, "__uloop_cb");
	lua_pushvalue(L, -2);
	ref = luaL_ref(L, -2);

	tout = lua_newuserdata(L, sizeof(struct lua_uloop_timeout));
	lua_pushvalue(L, -1);
	self_ref = luaL_ref(L, -3);

	lua_createtable(L, 0, 2);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	// lua_pushcfunction(L, ul_timer_free);
	// lua_setfield(L, -2, "__gc");
	lua_pushvalue(L, -1);
	lua_setmetatable(L, -3);
	lua_pushvalue(L, -2);
	luaI_openlib(L, NULL, timer_m, 1);
	lua_pushvalue(L, -2);

	memset(tout, 0, sizeof(*tout));

	tout->r = ref;
	tout->self_ref = self_ref;
	tout->t.cb = ul_timer_cb;
	if (have_set) {
		uloop_timeout_set(&tout->t, set);
    }

	return 1;
}

static int ul_process_pid(lua_State *L)
{
	struct lua_uloop_process *proc;

	proc = lua_touserdata(L, 1);
	lua_pushinteger(L, proc->p.pid);
	return 1;
}

static int _ul_process_signal(lua_State *L, int signal)
{
	struct lua_uloop_process *proc;
	int pid, ret;

	proc = lua_touserdata(L, 1);
	pid = proc->p.pid;
	ret = kill(pid, signal);
	lua_pushinteger(L, ret);
	return 1;
}

static int ul_process_term(lua_State *L) {
	return _ul_process_signal(L, SIGTERM);
}

static int ul_process_kill(lua_State *L) {
	return _ul_process_signal(L, SIGKILL);
}

static const luaL_Reg process_m[] = {
	{ "get_pid", ul_process_pid },
	{ "sigterm", ul_process_term },
	{ "sigkill", ul_process_kill },
	{ NULL, NULL }
};

static void ul_process_cb(struct uloop_process *p, int ret)
{
	struct lua_uloop_process *proc = container_of(p, struct lua_uloop_process, p);

	lua_getglobal(state, "__uloop_cb");
	lua_rawgeti(state, -1, proc->r);

	luaL_unref(state, -2, proc->r);
	luaL_unref(state, -2, proc->self_ref);

	lua_pushinteger(state, ret >> 8);
	lua_call(state, 1, 0);
	lua_pop(state, 1);
}

static int ul_process(lua_State *L)
{
	struct lua_uloop_process *proc;
	pid_t pid;
	int ref;
	int self_ref;

	const char* redirect_output_fname = NULL;
	int redirect_output_fd = -1;

	if (!lua_isfunction(L, -1)							 // callback
			|| !(lua_isstring(L, -2) || lua_isnil(L, -2))  // fname to redirect output
			|| !(lua_istable(L, -3) || lua_isnil(L, -3))   // env table
			|| !lua_istable(L, -4)						 // arg table
			|| !lua_isstring(L, -5)) {					 // process name
		lua_pushstring(L, "invalid arg list");
		lua_error(L);

		return 0;
	}

	if (lua_isstring(L, -2)) {
		redirect_output_fname = lua_tostring(L, -2);
		redirect_output_fd = open(redirect_output_fname, O_WRONLY);
		if (redirect_output_fd == -1) {
			lua_pushstring(L, "Couldn\'t open output file for writing");
			lua_error(L);

			return 0;
		}
	}

	pid = fork();

	if (pid == -1) {
		lua_pushstring(L, "failed to fork");
		lua_error(L);

		return 0;
	}

	if (pid == 0) {
		/* child */
		int argn = lua_objlen(L, -4);
		int envn = lua_objlen(L, -3);
		char** argp = malloc(sizeof(char*) * (argn + 2));
		char** envp = malloc(sizeof(char*) * (envn + 1));
		int i = 1;

		argp[0] = (char*) lua_tostring(L, -5);
		for (i = 1; i <= argn; i++) {
			lua_rawgeti(L, -4, i);
			argp[i] = (char*) lua_tostring(L, -1);
			lua_pop(L, 1);
		}
		argp[i] = NULL;

		for (i = 1; i <= envn; i++) {
			lua_rawgeti(L, -3, i);
			envp[i - 1] = (char*) lua_tostring(L, -1);
			lua_pop(L, 1);
		}
		envp[i - 1] = NULL;

        if (redirect_output_fname) {
            dup2(redirect_output_fd, STDOUT_FILENO);
            dup2(redirect_output_fd, STDERR_FILENO);
            close(redirect_output_fd);
        }

		if (lua_isnil(L, -3)) {
			execvp(*argp, argp);
		} else {
			execve(*argp, argp, envp);
		}
		exit(-1);
	}

    if (redirect_output_fname) {
        close(redirect_output_fd);
    }

	lua_getglobal(L, "__uloop_cb");
	lua_pushvalue(L, -2);
	ref = luaL_ref(L, -2);

	// This creates an opaque object that has methods associated with it
	// (defined in process_m)
	proc = lua_newuserdata(L, sizeof(*proc));
	lua_pushvalue(L, -1);
	self_ref = luaL_ref(L, -3);

	lua_createtable(L, 0, 2);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	lua_pushvalue(L, -1);
	lua_setmetatable(L, -3);
	lua_pushvalue(L, -2);
	luaI_openlib(L, NULL, process_m, 1);
	lua_pushvalue(L, -2);

	memset(proc, 0, sizeof(*proc));

	proc->r = ref;
	proc->self_ref = self_ref;
	proc->p.pid = pid;
	proc->p.cb = ul_process_cb;
	uloop_process_add(&proc->p);

	return 1;
}

static int ul_init(lua_State *L)
{
	uloop_init();
	lua_pushboolean(L, 1);

	return 1;
}

static int ul_run(lua_State *L)
{
	uloop_run();
	lua_pushboolean(L, 1);

	return 1;
}

static int ul_cancel(lua_State *L)
{
	uloop_cancel();
	lua_pushboolean(L, 1);

	return 1;
}

static luaL_reg uloop_func[] = {
	{"init", ul_init},
	{"run", ul_run},
	{"cancel", ul_cancel},
	{"timer", ul_timer},
	{"process", ul_process},
	{NULL, NULL},
};

/* avoid warnings about missing declarations */
int luaopen_uloop(lua_State *L);
int luaclose_uloop(lua_State *L);

int luaopen_uloop(lua_State *L)
{
	state = L;

	lua_createtable(L, 1, 0);
	lua_setglobal(L, "__uloop_cb");

	luaL_openlib(L, "uloop", uloop_func, 0);
	lua_pushstring(L, "_VERSION");
	lua_pushstring(L, "1.0");
	lua_rawset(L, -3);

	return 1;
}

int luaclose_uloop(lua_State *L)
{
	lua_pushstring(L, "Called");

	return 1;
}
