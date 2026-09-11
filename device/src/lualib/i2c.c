/* Lua linux i2c library. From Dean Blacketter */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>

#include "beep/debug.h"
#include "beep/i2c.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"


/* prototypes */
LUALIB_API int i2c_write(lua_State *L);
LUALIB_API int i2c_read(lua_State *L);

LUALIB_API int i2c_help(lua_State *L){
    lua_pushstring(L, "usage:\n"

                      "status = write(bus(int), device(int), register(int), data(int))...\n"
                      "status, data... = read(bus(int), device(int), register(int), [count(int)])\n"
                      "        (optional count for number of consecutive registers to read)\n"
                      "   status ok: ack=0\n"
                      "   status error: nack=1, send error=2, bus error=3, parameter error=4\n"

                      "version - version string\n");
    return 1;
}


/* LUA parameters:
 * int i2c bus number
 * int address
 * int starting register
 * int data bytes...
 *
 * Return values:
 * status
 * Number of bytes written
 */
LUALIB_API int i2c_write(lua_State *L){
    int bus;
    int address;
    int reg;

    int wlen = 0;
    unsigned char *wptr = 0;

    if (lua_gettop(L) < 4) {
        return luaL_error(L, "Wrong number of arguments");
    }
    bus = lua_tointeger(L, 1);
    address = lua_tointeger(L, 2);
    reg = lua_tointeger(L, 3);

    if (lua_isnumber(L, 4)) {
        /* buffer length is number of bytes pushed after bus, address and reg plus one byte for the reg address */
        wlen = lua_gettop(L) - 3 + 1;

        wptr = malloc(wlen);
        if (!wptr) {
            return luaL_error(L, "Malloc failed for i2c write buffer");
        }

        /* first byte is the register address */
        wptr[0] = reg;

        /* subsequent bytes are pulled from the stack */
        for (int i = 1; i < wlen; i++) {
            wptr[i] = lua_tointeger(L, i + 3);
        }
    } else if (lua_istable(L, 4)) {
        // djb - todo: accept an array of values. (already accept a series of parameters above), until then use unpack
    }

    // write back i2c write status
    lua_pushnumber(L, i2c_rw(bus, address, wptr, wlen, 0, 0));
    if (wptr) {
        free(wptr);
    }

    return 1;
}

/* LUA
 * Parameters:
 * int bus
 * int address
 * int reg
 * int count
 *
 * Return:
 * Status
 * data...
 */
LUALIB_API int i2c_read(lua_State *L){
    int bus;
    int address;
    int reg;
    int count;
    unsigned char *rdata;
    unsigned char regbyte;

    if (lua_gettop(L) < 3){
        return luaL_error(L, "Wrong number of arguments");
    }

    // parse input
    bus = lua_tointeger (L, 1);
    address = lua_tointeger(L, 2);
    reg = lua_tointeger(L, 3);
    count = (lua_gettop(L) == 4) ? lua_tointeger(L, 4) : 1;

    rdata = (unsigned char *)malloc(count);
    if (!rdata) {
        return luaL_error(L, "Malloc failed for i2c read buffer");
    }

    regbyte = reg;
    /* do a dummy write of a single byte to set the register position that we want to read */
    /* followed by the actual read of requested bytes */
    int fail = i2c_rw(bus, address, &regbyte, sizeof(regbyte), rdata, count);

    if (fail != RW_ACK) {
        return luaL_error(L, "i2c_rw failed with %d", fail);
        if (rdata) {
            free(rdata);
        }
    }

    lua_newtable(L);

    for (int i = 0; i < count; i++)  {
        lua_pushnumber(L, i+1); // lua is one based
        lua_pushnumber(L, rdata[i]);
        lua_settable(L, -3);
    }

    if (rdata) {
        free(rdata);
    }

    return 1;
}

#define IOMCU_CRC8_INIT                             (0xFF)

// simple crc8: poly = 0xd5, init = 0x55.
static uint8_t crc8(uint8_t crc, uint8_t *buf, int len) {
    uint8_t i;
    while (len--) {
        crc ^= *buf++;
        for (i = 0; i < 8; i++) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0xd5;
            else
                crc <<= 1;
        }
    }
    return crc;
}

int i2c_message_verify(uint8_t *buf, uint8_t regaddr, int len,
        uint8_t crc_byte) {
    return crc8(IOMCU_CRC8_INIT + regaddr, buf, len) == crc_byte;
}

// Does an i2c read, expects crc byte as last byte of message, confirms
// crc matches.
LUALIB_API int i2c_read_and_verify1(lua_State *L){
    int bus;
    int address;
    int reg;
    int count;
    unsigned char *rdata;
    unsigned char regbyte;

    if (lua_gettop(L) < 3){
        return luaL_error(L, "Wrong number of arguments");
    }

    // parse input
    bus = lua_tointeger (L, 1);
    address = lua_tointeger(L, 2);
    reg = lua_tointeger(L, 3);
    count = ((lua_gettop(L) == 4) ? lua_tointeger(L, 4) : 1) + 1;

    rdata = (unsigned char *)alloca(count);

    regbyte = reg;
    /* do a dummy write of a single byte to set the register position that we want to read */
    /* followed by the actual read of requested bytes */
    int fail = i2c_rw(bus, address, &regbyte, sizeof(regbyte), rdata, count);

    if (fail != RW_ACK) {
        return luaL_error(L, "i2c_rw failed with %d", fail);
        if (rdata) {
            free(rdata);
        }
    }

    if (!i2c_message_verify(rdata, regbyte, count - 1, rdata[count - 1])) {
        LOG_ERROR(log_beep_main, "i2c_read2 crc8 failed");
        return luaL_error(L, "i2c_read crc8 failed");
    }

    lua_newtable(L);

    for (int i = 0; i < count - 1; i++)  {
        lua_pushnumber(L, i+1); // lua is one based
        lua_pushnumber(L, rdata[i]);
        lua_settable(L, -3);
    }

    return 1;
}

// Same as i2c_read_and_verify1 but expects crc byte at beginning of message.
LUALIB_API int i2c_read_and_verify2(lua_State *L){
    int bus;
    int address;
    int reg;
    int count;
    unsigned char *rdata;
    unsigned char regbyte;

    if (lua_gettop(L) < 3){
        return luaL_error(L, "Wrong number of arguments");
    }

    // parse input
    bus = lua_tointeger (L, 1);
    address = lua_tointeger(L, 2);
    reg = lua_tointeger(L, 3);
    count = ((lua_gettop(L) == 4) ? lua_tointeger(L, 4) : 1) + 1;

    rdata = (unsigned char *)alloca(count);

    regbyte = reg;
    /* do a dummy write of a single byte to set the register position that we want to read */
    /* followed by the actual read of requested bytes */
    int fail = i2c_rw(bus, address, &regbyte, sizeof(regbyte), rdata, count);

    if (fail != RW_ACK) {
        return luaL_error(L, "i2c_rw failed with %d", fail);
        if (rdata) {
            free(rdata);
        }
    }

    if (!i2c_message_verify(rdata + 1, regbyte, count - 1, rdata[0])) {
        LOG_ERROR(log_beep_main, "i2c_read2 crc8 failed");
        return luaL_error(L, "i2c_read crc8 failed");
    }

    lua_newtable(L);

    for (int i = 0; i < count - 1; i++)  {
        lua_pushnumber(L, i+1); // lua is one based
        lua_pushnumber(L, rdata[i+1]);
        lua_settable(L, -3);
    }

    return 1;
}

LUALIB_API int i2c_write_with_checksum(lua_State *L){
    int bus;
    int address;
    int reg;

    int wlen = 0;
    unsigned char *wptr = 0;

    if (lua_gettop(L) < 4) {
        return luaL_error(L, "Wrong number of arguments");
    }
    bus = lua_tointeger(L, 1);
    address = lua_tointeger(L, 2);
    reg = lua_tointeger(L, 3);

    if (lua_isnumber(L, 4)) {
        /* buffer length is number of bytes pushed after bus, address and reg plus one byte for the reg address and one byte for checksum*/
        wlen = lua_gettop(L) - 3 + 2;

        wptr = malloc(wlen);
        if (!wptr) {
            return luaL_error(L, "Malloc failed for i2c write buffer");
        }

        /* first byte is the register address */
        wptr[0] = reg;

        /* subsequent bytes are pulled from the stack */
        for (int i = 2; i < wlen; i++) {
            wptr[i] = lua_tointeger(L, i + 2);
        }

        wptr[1] = crc8(IOMCU_CRC8_INIT + reg, &wptr[2], wlen - 2);
    } else if (lua_istable(L, 4)) {
        // djb - todo: accept an array of values. (already accept a series of parameters above), until then use unpack
    }

    // write back i2c write status
    lua_pushnumber(L, i2c_rw(bus, address, wptr, wlen, 0, 0));
    if (wptr) {
        free(wptr);
    }

    return 1;
}

/* functions exposed to lua */
static const luaL_reg i2c_functions[] = {
    {"write", i2c_write},
    {"read", i2c_read},
    {"read_and_verify1", i2c_read_and_verify1},
    {"read_and_verify2", i2c_read_and_verify2},
    {"write_with_checksum", i2c_write_with_checksum},
    {"help", i2c_help},
    {NULL, NULL}
};


/* init function, will be called when lua run require */
LUALIB_API int luaopen_i2c (lua_State *L) {
    log_beep_main = LOG_CATEGORY_GET("i2c.so");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    luaL_openlib(L, "i2c", i2c_functions, 0);
    return 1;
}
