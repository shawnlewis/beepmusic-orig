#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <libubox/uloop.h>

#include "beep/debug.h"
#include "beep/flags.h"
#include "beep/beep_ubus.h"
#include "beep/beep_data.h"

#define TEST_SET

static void get_callback(const char *key, const void *value,
        size_t len, void *userdata) {
    LOG_DEBUG(log_beep_main, "GOT: %s = %s (%zu bytes)", key, (char *)value, len);
}

#define NUM_CONCURRENT 32

static void delete_callback(const char *key, bool ok, void *userdata) {
    LOG_DEBUG(log_beep_main, "key = %s, ok = %s", key, ok ? "OK" : "NOPE!");
}

static void start_deletes(struct uloop_timeout *t) {
    for(int x=0;x<NUM_CONCURRENT;x++) {
        char key[64];
        snprintf(key, 64, "test_key_%d", x);
        beep_data_delete(key, true, &delete_callback, NULL);
    }
}

static void start_gets(struct uloop_timeout *t) {
    for(int x=0;x<NUM_CONCURRENT;x++) {
        char key[64];
        snprintf(key, 64, "test_key_%d", x);
        beep_data_get(key, true, &get_callback, NULL);
    }

    t->cb = start_deletes;
    uloop_timeout_set(t, 5000);
}

static void set_callback(const char *key, bool ok, void *userdata) {
    LOG_DEBUG(log_beep_main, "key = %s, ok = %s", key, ok ? "OK" : "NOPE!");
}

static void start_sets(void) {
    for(int x=0;x<NUM_CONCURRENT;x++) {
        char key[64];
        char val[80] = {0};
        snprintf(key, 64, "test_key_%d", x);
        snprintf(val, 80, "`~!@#$%%^&*()-_=+\\|/?[]{}'\";:,.<> abcdefg 123456 %c (%d)", 0, x);
        beep_data_set(key, val, 80, true, &set_callback, NULL);
    }
}

static struct uloop_timeout t = {
    .cb = start_gets,
};

int main(int argc, char **argv) {
    log_beep_main = LOG_CATEGORY_GET("data_test");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);
    struct ubus_context *ctx;

    beep_flags_init(argc, argv);

    uloop_init();

    ctx = beep_ubus_connect("data_test");

    start_sets();

    uloop_timeout_set(&t, 5000);

    uloop_run();

    beep_ubus_disconnect(ctx);
}
