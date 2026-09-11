#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <stdint.h>
#include <string.h>

#include <openssl/evp.h>

#include "beep/debug.h"

static EVP_MD_CTX *mdctx;
static const char hex[] = "0123456789abcdef";

static char *do_hash(const char *msg, size_t msg_len,
        const EVP_MD *md, size_t *digest_len) {
    static char hex_digest[EVP_MAX_MD_SIZE * 2] = {0};

    unsigned char digest[EVP_MAX_MD_SIZE] = {0};
    int i;

    EVP_DigestInit_ex(mdctx, md, NULL);
    EVP_DigestUpdate(mdctx, (const unsigned char *)msg, msg_len);
    EVP_DigestFinal_ex(mdctx, digest, (unsigned int *)digest_len);

    for(i = 0; i < *digest_len; i++) {
        hex_digest[i * 2] = hex[digest[i] >> 4];
        hex_digest[(i * 2) + 1] = hex[digest[i] & 0xf];
    }

    return hex_digest;
}

static int lua_crypto_sha1(lua_State *L) {
    size_t msg_len = 0;
    size_t digest_len = 0;
    const char *msg = lua_tolstring(L, 1, &msg_len);

    char *hex_digest = do_hash(msg, msg_len, EVP_sha1(), &digest_len);

    lua_pushlstring(L, hex_digest, digest_len * 2);
    return 1;
}

static int lua_crypto_md5(lua_State *L) {
    size_t msg_len = 0;
    size_t digest_len = 0;
    const char *msg = lua_tolstring(L, 1, &msg_len);

    char *hex_digest = do_hash(msg, msg_len, EVP_md5(), &digest_len);

    lua_pushlstring(L, hex_digest, digest_len * 2);
    return 1;
}

static int lua_crypto_init(lua_State *L) {
    mdctx = EVP_MD_CTX_create();

    lua_pushboolean(L, (uint8_t)1);
    return 1;
}

static int lua_crypto_free(lua_State *L) {
    EVP_MD_CTX_destroy(mdctx);

    lua_pushboolean(L, (uint8_t)1);
    return 1;
}

static luaL_reg crypto_func[] = {
    {"init", lua_crypto_init},
    {"free", lua_crypto_free},
    {"sha1", lua_crypto_sha1},
    {"md5", lua_crypto_md5},
    {NULL, NULL}
};

int luaopen_crypto(lua_State *L) {
    log_beep_main = LOG_CATEGORY_GET("crypto.so");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    luaL_openlib(L, "crypto", crypto_func, 0);

    return 1;
}

int luaclose_beepnl(lua_State *L) {
    return 1;
}

