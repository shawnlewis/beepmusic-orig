#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "beep/debug.h"
#include "beep/beep_ubus.h"

#include "wifisetup.h"


static bool sta_client;
static int scan_ap_count = 3;
static unsigned int scan_ms_delay;
static ConnStatus conn_stat;
static unsigned int conn_ms_delay;

enum {
    SET_MODE_STA_CLIENT,
    __SET_MODE_MAX
};

static const struct blobmsg_policy set_mode_policy[] = {
    [SET_MODE_STA_CLIENT] = { .name = "sta_client", .type = BLOBMSG_TYPE_INT32 }
};

static int ws_debug_set_mode(struct ubus_context *ctx,
        struct ubus_object *obj, struct ubus_request_data *req,
        const char *method, struct blob_attr *msg) {
    struct blob_attr *tb[__SET_MODE_MAX];

    blobmsg_parse(set_mode_policy, ARRAY_SIZE(set_mode_policy),
            tb, blob_data(msg), blob_len(msg));

    if (!tb[SET_MODE_STA_CLIENT]) {
        beep_reply_error(ubus_ctx, req, "missing sta_client",
                BEEP_UBUS_ERROR_ARGS);
        return 0;
    } else {
        sta_client = !!blobmsg_get_u32(tb[SET_MODE_STA_CLIENT]);
    }

    beep_reply_success(ctx, req, NULL);

    return 0;
}

enum {
    SET_SCAN_PARAM_AP_COUNT,
    SET_SCAN_PARAM_MS_DELAY,
    __SET_SCAN_PARAM_MAX
};

static const struct blobmsg_policy set_scan_param_policy[] = {
    [SET_SCAN_PARAM_AP_COUNT] = { .name = "ap_count", .type = BLOBMSG_TYPE_INT32 },
    [SET_SCAN_PARAM_MS_DELAY] = { .name = "ms_delay", .type = BLOBMSG_TYPE_INT32 }
};

static int ws_debug_set_scan_param(struct ubus_context *ctx,
        struct ubus_object *obj, struct ubus_request_data *req,
        const char *method, struct blob_attr *msg) {
    struct blob_attr *tb[__SET_SCAN_PARAM_MAX];

    blobmsg_parse(set_scan_param_policy, ARRAY_SIZE(set_scan_param_policy),
            tb, blob_data(msg), blob_len(msg));

    if (!tb[SET_SCAN_PARAM_AP_COUNT]) {
        scan_ap_count = 0;
    } else {
        scan_ap_count = (int)blobmsg_get_u32(tb[SET_SCAN_PARAM_AP_COUNT]);
    }

    if (!tb[SET_SCAN_PARAM_MS_DELAY]) {
        scan_ms_delay = 0;
    } else {
        scan_ms_delay = (unsigned int)
                blobmsg_get_u32(tb[SET_SCAN_PARAM_MS_DELAY]);
    }

    beep_reply_success(ctx, req, NULL);

    return 0;
}

enum {
    SET_CONN_PARAM_CONN_STAT,
    SET_CONN_PARAM_MS_DELAY,
    __SET_CONN_PARAM_MAX
};

static const struct blobmsg_policy set_conn_param_policy[] = {
    [SET_CONN_PARAM_CONN_STAT] = { .name = "conn_stat", .type = BLOBMSG_TYPE_INT32 },
    [SET_CONN_PARAM_MS_DELAY] = { .name = "ms_delay", .type = BLOBMSG_TYPE_INT32 }
};

static int ws_debug_set_conn_param(struct ubus_context *ctx,
        struct ubus_object *obj, struct ubus_request_data *req,
        const char *method, struct blob_attr *msg) {
    struct blob_attr *tb[__SET_CONN_PARAM_MAX];

    blobmsg_parse(set_conn_param_policy, ARRAY_SIZE(set_conn_param_policy),
            tb, blob_data(msg), blob_len(msg));

    if (!tb[SET_CONN_PARAM_CONN_STAT]) {
        conn_stat = CONN_STAT_OK;
    } else {
        conn_stat = (ConnStatus)blobmsg_get_u32(tb[SET_CONN_PARAM_CONN_STAT]);
    }

    if (!tb[SET_CONN_PARAM_MS_DELAY]) {
        conn_ms_delay = 0;
    } else {
        conn_ms_delay = (unsigned int)
                blobmsg_get_u32(tb[SET_CONN_PARAM_MS_DELAY]);
    }

    beep_reply_success(ctx, req, NULL);

    return 0;
}

static const struct ubus_method ws_debug_methods[] = {
    UBUS_METHOD("set_mode", ws_debug_set_mode, set_mode_policy),
    UBUS_METHOD("set_scan_param", ws_debug_set_scan_param,
            set_scan_param_policy),
    UBUS_METHOD("set_conn_param", ws_debug_set_conn_param,
            set_conn_param_policy)
};

static struct ubus_object_type ws_debug_object_type =
    UBUS_OBJECT_TYPE("beep.wifisetup.debug", ws_debug_methods);

static struct ubus_object ws_debug_object = {
    .name = "beep.wifisetup.debug",
    .type = &ws_debug_object_type,
    .methods = ws_debug_methods,
    .n_methods = ARRAY_SIZE(ws_debug_methods),
};

static void ws_debug_sleep(unsigned int ms) {
    unsigned int sec = ms / 1000;
    unsigned int usec = (ms - (sec * 1000)) * 1000;
    if (sec)
        sleep(sec);
    if (usec)
        usleep(sec);
}

static uint8_t rand_u8(void) {
    static int c = 0;
    static long r;

    uint8_t d;

    if (!c) {
        r = random();
        c = 4;
    }

    d = r & 0xff;
    r >>= 8;
    c--;
    return d;
}

void ws_netbe_cleanup(void) {
    if (ubus_ctx) {
        ubus_remove_object(ubus_ctx, &ws_debug_object);
    }
}

// The debug backend will just return static values without ubus running.
int ws_netbe_init(void) {
    int ret = 0;
    if (ubus_ctx) {
        ret = ubus_add_object(ubus_ctx, &ws_debug_object);
        if (ret) {
            LOG_ERROR(log_beep_main, "failed to add debug object: %s", ubus_strerror(ret));
        }
    }
    return ret;
}

OPMode ws_netbe_mode(void) {
    return sta_client ? OP_MODE_CLIENT : OP_MODE_STA;
}

APList *ws_netbe_scan(void) {
    APList *aplist = ws_aplist_init(scan_ap_count);
    int i;
    int j;

    assert(aplist);

    for (i = 0; i < aplist->count; i++) {
        assert(asprintf(&aplist->aps[i].essid, "debug-net-%u",
                rand_u8()) != -1);
        aplist->aps[i].enc_type = (APEncType)((rand_u8() % (ENC_TYPE_MAX - ENC_TYPE_MIN + 1))
                + ENC_TYPE_MIN);
        for (j = 0; j < 6; j++) {
            aplist->aps[i].bssid[j] = rand_u8();
        }
        aplist->aps[i].signal = (int8_t)rand_u8();
        aplist->aps[i].channel = (rand_u8() % 11) + 1;
    }

    ws_debug_sleep(scan_ms_delay);
    return aplist;
}

ConnStatus ws_netbe_connect(const AP *ap, bool force) {
    ws_debug_sleep(conn_ms_delay);
    switch (conn_stat) {
    case CONN_STAT_OK:
    case CONN_STAT_ERROR:
        return conn_stat;
    }
    return CONN_STAT_ERROR;
}
