#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "beep/beeplib.h"

// Splits a 64-bit value into two 32-bit components and pushes them onto
// the lua stack.
static void pushinteger64(lua_State *L, uint64_t n) {
    uint32_t high = n >> 32;
    uint32_t low = n & 0xffffffff;
    lua_pushinteger(L, high);
    lua_pushinteger(L, low);
}

static uint64_t combine32(uint32_t high, uint32_t low) {
    return  ((uint64_t) high << 32) | (uint64_t) low;
}

static int lua_beep_millis_to_string(lua_State *L) {
    uint32_t now_high = lua_tointeger(L, 1);
    uint32_t now_low = lua_tointeger(L, 2);
    uint64_t now = combine32(now_high, now_low);
    char result[64];
    snprintf(result, 64, "%" PRIu64, now);
    lua_pushstring(L, result);
    return 1;
}

static int lua_beep_millis_to_log_time_string(lua_State *L) {
    uint32_t now_high = lua_tointeger(L, 1);
    uint32_t now_low = lua_tointeger(L, 2);
    uint64_t now = combine32(now_high, now_low);
    long int seconds = now / 1000;
    long int milliseconds = now % 1000;
    struct tm tm;
    char result[64];
    gmtime_r((const time_t*) &seconds, &tm);
    // Format string taken from lib/beep/log.c
    sprintf(result, "%04d%02d%02d %02d:%02d:%02d.%03ld",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec,
            milliseconds);
    lua_pushstring(L, result);
    return 1;
}

// Returns 64-bit millisecond count using pushinteger64
static int lua_beep_millis(lua_State *L) {
    uint64_t now = beep_millis();
    pushinteger64(L, now);
    return 2;
}

// Adds argument 3 to the 64-bit value specified by arguments 1 and 2.
static int lua_beep_millis_add(lua_State *L) {
    uint32_t now_high = lua_tointeger(L, 1);
    uint32_t now_low = lua_tointeger(L, 2);
    uint32_t inc_by = lua_tointeger(L, 3);

    uint64_t total = ((uint64_t) now_high << 32) + now_low + inc_by;

    pushinteger64(L, total);
    return 2;
}

// Adds argument 3 to the 64-bit value specified by arguments 1 and 2.
static int lua_beep_millis_sub(lua_State *L) {
    uint32_t minuend_high = lua_tointeger(L, 1);
    uint32_t minuend_low = lua_tointeger(L, 2);
    uint32_t subtrahend_high = lua_tointeger(L, 3);
    uint32_t subtrahend_low = lua_tointeger(L, 4);

    uint64_t diff = combine32(minuend_high, minuend_low)
        - combine32(subtrahend_high, subtrahend_low);

    pushinteger64(L, diff);
    return 2;
}

static const luaL_Reg beep_reg[] = {
    {"beep_millis", lua_beep_millis},
    {"beep_millis_add", lua_beep_millis_add},
    {"beep_millis_sub", lua_beep_millis_sub},
    {"beep_millis_to_string", lua_beep_millis_to_string},
    {"beep_millis_to_log_time_string", lua_beep_millis_to_log_time_string},
    {NULL, NULL},
};

int luaopen_beep(lua_State *L) {
    luaL_openlib (L, "beep", beep_reg, 0);

    return 1;
}
