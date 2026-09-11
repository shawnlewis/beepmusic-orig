#include <libubox/list.h>
#include <libubox/uloop.h>
#include <libubox/usock.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <stdio.h>
#include <unistd.h>

#include "beep/debug.h"
#include "beep/gpiopoll.h"
#include "beep/registers.h"

#define SOCK_FILE "/tmp/beepdevio.sock"

#define ROT_CW ROT_A
#define ROT_CCW ROT_B

lua_State *state;

static bool started = false;

struct uloop_fd uloop_fd_read;
struct uloop_fd uloop_fd_write;

struct data_pack {
    int input;
    bool on;
};
#define PACK_SIZE sizeof(struct data_pack)

int writable_fd = -1;

void _input_callback(int input, bool on) {
    if(writable_fd < 0)
        return;

    struct data_pack data = {
        .input = input,
        .on = on
    };

    write(writable_fd, &data, PACK_SIZE);
}

static void read_cb(struct uloop_fd *u, unsigned int events) {
    struct data_pack data;

    read(uloop_fd_read.fd, &data, PACK_SIZE);

    lua_getglobal(state, "__gpio_cb");                              // -0,+1
    lua_pushinteger(state, data.input);                             // -0,+1
    lua_pushboolean(state, data.on);                                // -0,+1
    lua_call(state, 2, 0);                                          // -3,+0
}

static int gpio_init(lua_State *L) {

    bool use_gpio;
    unsigned int len;
    struct sockaddr_in sin;

    if (!lua_isnumber(L, -1)) {
        lua_pushstring(L, "must provide use_gpio (bool)");
        lua_error(L);

        return 0;
    }

    use_gpio = lua_tointeger(L, -1) != 0;
    lua_pop(L, 1);

    if (!lua_isfunction(L, -1)) {
        lua_pushstring(L, "must provide callback: input_callback(input, on)");
        lua_error(L);

        return 0;
    }

    unlink(SOCK_FILE);

    // Create our write_fd first.
    int write_fd = usock(USOCK_SERVER|USOCK_UNIX, SOCK_FILE, NULL);

    int read_fd = usock(USOCK_UNIX, SOCK_FILE, NULL);
    uloop_fd_read.fd = read_fd;
    uloop_fd_read.cb = read_cb;
    uloop_fd_add(&uloop_fd_read, ULOOP_READ);

    // After creation of the read fd we have a connection ready to accept
    len = sizeof(struct sockaddr_in);
    writable_fd = accept(write_fd, (struct sockaddr*) &sin, &len);

    lua_setglobal(L, "__gpio_cb");                                  // -1,+0

    beep_gpio_init(use_gpio);

    lua_pushboolean(L, 1);
    return 1;
}

static int gpio_enable_input(lua_State *L) {
    int gpio = luaL_checkinteger(L, 1);
    lua_pushboolean(L, beep_gpio_enable_input(gpio));

    return 1;
}

static int gpio_enable_output(lua_State *L) {
    int gpio = luaL_checkinteger(L, 1);
    lua_pushboolean(L, beep_gpio_enable_output(gpio));

    return 1;
}

static int gpio_start(lua_State *L) {
    if (!started) {
        started = true;
        beep_gpio_start(_input_callback);
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int gpio_stop(lua_State *L) {
    if (started) {
        started = false;
        beep_gpio_end();
        uloop_fd_delete(&uloop_fd_read);
    }

    lua_pushboolean(L, 1);
    return 1;
}

static bool _set_led(int led, int on_frac) {
    /* Silently restrict on_frac range */
    if(on_frac > 1000)
        on_frac = 1000;
    else if(on_frac < 0)
        on_frac = 0;

    return(beep_gpio_set_led_on_frac(led, on_frac));
}

static int gpio_set_led(lua_State *L) {
    int led = luaL_checkinteger(L, 1);
    int on_frac = luaL_checkinteger(L, 2);

    bool ret = _set_led(led, on_frac);
    lua_pushboolean(L, ret ? 1 : 0);                                // -0,+1
    return 1;
}

static int gpio_commit(lua_State *L) {
    beep_gpio_commit_leds();

    return 0;
}

// Taken from: http://lua-users.org/lists/lua-l/2010-06/msg00533.html
// Allows us to run cleanup code when the module is gc'd, so that we can
// stop the gpiopoll thread properly and avoid a segfault.
static void gc_sentinel(lua_State * L, int idx, lua_CFunction callback) {

    lua_pushvalue(L, idx); // value @idx
    lua_newuserdata(L, sizeof(void *)); // sentinel userdata
        lua_newtable(L);    // userdata metatable with __gc = callback
        lua_pushcfunction(L, callback);
        lua_setfield(L, -2, "__gc");
        lua_setmetatable(L, -2);

    /* check for (weak-valued) sentinel table; create if needed */
    lua_getfield(L, LUA_REGISTRYINDEX, "__gc_sentinels");
    if (lua_isnoneornil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        // make weak-keyed
        lua_pushstring(L, "v");
        lua_setfield(L, -2, "__mode");
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        lua_pushvalue(L, -1);
        lua_setmetatable(L, -2);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "__gc_sentinels");
    }

    lua_insert(L, -3);
    lua_insert(L, -2);
    lua_settable(L, -3); // lua::sentinel[value @idx] = sentinel userdata
    lua_pop(L, 1); // lua::sentinel
}

static int luaclose_mymodule(lua_State * L) {
    gpio_stop(L);
    return 0;
}

static luaL_reg gpio_func[] = {
    {"init", gpio_init},
    {"enable_input", gpio_enable_input},
    {"enable_output", gpio_enable_output},
    {"start", gpio_start},
    {"stop", gpio_stop},
    {"set_led", gpio_set_led},
    {"commit", gpio_commit},
    {NULL, NULL}
};

int luaopen_gpio(lua_State *L) {
    log_beep_main = LOG_CATEGORY_GET("gpio.so");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    state = L;

    luaL_openlib(L, "gpio", gpio_func, 0);
    gc_sentinel(L, -1, luaclose_mymodule);

    return 1;
}
