#include <pthread.h>

#include "wifisetup.h"

#include "beep/config.h"
#include "beep/debug.h"
#include "beep/beep_ubus.h"
#include "beep/beeplib.h"


// Timing values in milliseconds.
#define WIFI_SCAN_MS_PERIOD                         (300 * 1000)
#define WIFI_SCAN_NOW                               (-1)
#define WIFI_FAST_SCAN_MS_PERIOD                    (8 * 1000)
#define WIFI_FAST_SCAN_ITERATIONS                   (2)
#define DEFAULT_CONNECT_CONFIRM_TIME                (10 * 1000)

#define WSD_BG_ULOOP_TIMER                          (100)

#define WSD_BG_STATE_ERROR                          (-1)
#define WSD_BG_STATE_IDLE                           (0)
#define WSD_BG_STATE_STARTING                       (1)
#define WSD_BG_STATE_STOPPING                       (2)
#define WSD_BG_STATE_SCAN                           (3)
#define WSD_BG_STATE_CONNECT                        (4)
#define WSD_BG_STATE_CONFIRM_WAIT                   (5)

#define WSD_SHUTDOWN_STATE_RUN                      (0)
#define WSD_SHUTDOWN_STATE_STOPPING                 (1)
#define WSD_SHUTDOWN_STATE_JOINING                  (2)

#define WSD_BG_CONNECT_INTERNAL_ERROR               (-1)
#define WSD_BG_CONNECT_BUSY                         (-2)
#define WSD_BG_CONNECT_NA                           (-3)
#define WSD_BG_CONNECT_CONFIRM_TIMEOUT              (-4)

struct ConnectParams {
    AP ap;
    char *key;
    int confirm_counter;
    uint8_t return_on;
    bool shutdown_on_success;
};


static pthread_t wsd_bg_thread;
static pthread_mutex_t wsd_bg_mutex;
static pthread_cond_t wsd_bg_cond;
static pthread_mutex_t wsd_data_lock;

// Data used only by main thread.
static int scan_counter;

// See constants: WIFI_FAST_SCAN_MS_PERIOD, WIFI_FAST_SCAN_ITERATIONS
static int startup_fast_scan_counter;

// Data used only by wsd thread.


// Can be set by main or wsd thread (no locks).
// This isn't really locked but should only be set by the main thread when
// idle, and set by the background thread when not idle.
static int wsd_bg_state;
static int wsd_shutdown_state;
static bool wsd_save_on_exit;
// If conn_params is non-null a connection is being processed.
static struct ConnectParams *conn_params;

// Protected by wsd_data_lock.
static AP *scan_ap_head;
static OPMode opmode;
static int conn_reason;
static char *conn_reason_str;
// Derived from beep_millis, if 0 wsd_status should report back 0.
static uint32_t conn_sec;


enum {
    SHUTDOWN_SAVE,
    SHUTDOWN_MAINIO,
    __SHUTDOWN_MAX
};

static const struct blobmsg_policy shutdown_policy[] = {
    POLICY(SHUTDOWN_SAVE, "save", BLOBMSG_TYPE_INT32)
};

static int wsd_shutdown(struct ubus_context *ctx, struct ubus_object *obj,
              struct ubus_request_data *req, const char *method,
              struct blob_attr *msg) {
    struct blob_attr *tb[__SHUTDOWN_MAX];

    blobmsg_parse(shutdown_policy, ARRAY_SIZE(shutdown_policy),
            tb, blob_data(msg), blob_len(msg));

    if (tb[SHUTDOWN_SAVE]) {
        wsd_save_on_exit = (bool)blobmsg_get_u32(tb[SHUTDOWN_SAVE]);
    }

    beep_reply_success(ctx, req, NULL);

    wsd_shutdown_state = WSD_SHUTDOWN_STATE_STOPPING;
    return 0;
}

enum {
    RENAME_NAME,
    __RENAME_MAX
};

static const struct blobmsg_policy rename_policy[] = {
    POLICY(RENAME_NAME, "name", BLOBMSG_TYPE_STRING)
};

static int wsd_rename(struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    struct blob_attr *tb[__RENAME_MAX];

    blobmsg_parse(rename_policy, ARRAY_SIZE(rename_policy),
            tb, blob_data(msg), blob_len(msg));

    if(tb[RENAME_NAME]) {
        if(!beep_config_data_write("device_name",
                blobmsg_get_string(tb[RENAME_NAME]))) {
            LOG_ERROR(log_beep_main, "Failed to rename device");
            beep_reply_error(ctx, req, "Failed to rename device", 0);
        } else {
            beep_reply_success(ctx, req, NULL);
        }
    }

    return 0;
}

static int wsd_status(struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    struct blob_buf *args = calloc(1, sizeof(struct blob_buf));
    void *cookie;
    void *ap_cookie;
    AP *ap = scan_ap_head;

    blob_buf_init(args, 0);
    blobmsg_add_string(args, "device_id", ws_config->device_id);
    blobmsg_add_string(args, "device_name", ws_config->device_name);
    blobmsg_add_string(args, "status", opmodestr(opmode));

    pthread_mutex_lock(&wsd_data_lock);

    cookie = blobmsg_open_table(args, "assoc_status");
    // To prevent locking twice in wsd_bg thread fill in the busy state.
    if (wsd_bg_state == WSD_BG_STATE_CONNECT) {
        blobmsg_add_u32(args, "code", WSD_BG_CONNECT_BUSY);
        blobmsg_add_string(args, "msg", "busy");
        blobmsg_add_string(args, "user_id", "unknown");
        blobmsg_add_u32(args, "time", 0);
    } else {
        uint32_t sec = conn_sec ?
                (uint32_t)((beep_millis() / 1000)) - conn_sec : 0;

        blobmsg_add_u32(args, "code", conn_reason);
        blobmsg_add_string(args, "msg", conn_reason_str ?
                conn_reason_str : "none");
        blobmsg_add_string(args, "user_id", "unknown");
        blobmsg_add_u32(args, "time", sec);
    }
    blobmsg_close_table(args, cookie);

    cookie = blobmsg_open_array(args, "ap_list");
    while (ap) {
        // Wifisetup does not support hidden networks.  Skip over
        // sending them to the phone.
        if (ap->essid) {
            ap_cookie = blobmsg_open_table(args, "ap");
            blobmsg_add_string(args, "essid", ap->essid);
            blobmsg_add_string(args, "bssid", bssidstr(ap->bssid));
            blobmsg_add_string(args, "enc_type", encstr(ap->enc_type));
            blobmsg_add_u32(args, "signal", ap->signal);
            blobmsg_add_u32(args, "channel", ap->channel);
            blobmsg_close_table(args, ap_cookie);
        }
        ap = ap->next;
    }
    pthread_mutex_unlock(&wsd_data_lock);
    blobmsg_close_array(args, cookie);

    beep_reply_success(ctx, req, args->head);

    scan_counter = WIFI_SCAN_NOW;

    blob_buf_free(args);
    free(args);
    return 0;
}

enum {
    CONNECT_BSSID,
    CONNECT_ESSID,
    CONNECT_ENC_TYPE,
    CONNECT_KEY,
    CONNECT_RETURN_ON,
    CONNECT_CONFIRM_TIME,
    CONNECT_SHUTDOWN,
    __CONNECT_MAX
};

static const struct blobmsg_policy connect_policy[] = {
    POLICY(CONNECT_BSSID, "bssid", BLOBMSG_TYPE_STRING),
    POLICY(CONNECT_ESSID, "essid", BLOBMSG_TYPE_STRING),
    POLICY(CONNECT_ENC_TYPE, "enc_type", BLOBMSG_TYPE_STRING),
    POLICY(CONNECT_KEY, "key", BLOBMSG_TYPE_STRING),
    POLICY(CONNECT_RETURN_ON, "return_on", BLOBMSG_TYPE_STRING),
    POLICY(CONNECT_CONFIRM_TIME, "confirm_time", BLOBMSG_TYPE_INT32),
    POLICY(CONNECT_SHUTDOWN, "shutdown", BLOBMSG_TYPE_INT32)
};

static int wsd_connect(struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    struct blob_attr *tb[__CONNECT_MAX];
    struct ConnectParams *params;
    uint8_t enc_type = ENC_TYPE_UNKNOWN;
    uint8_t return_on = CONN_RETURN_ON_UNKNOWN;

    if (conn_params) {
        beep_reply_error(ctx, req, "service busy", 0);
        return 0;
    }

    blobmsg_parse(connect_policy, ARRAY_SIZE(connect_policy),
            tb, blob_data(msg), blob_len(msg));

    // Verify required arguments.
    if (!tb[CONNECT_ESSID]) {
        beep_reply_error(ctx, req, "missing essid", BEEP_UBUS_ERROR_ARGS);
        return 0;
    }

    if (!tb[CONNECT_ENC_TYPE]) {
        beep_reply_error(ctx, req, "missing enc_type", BEEP_UBUS_ERROR_ARGS);
        return 0;
    } else {
        // Check that enc_type is valid and if a key is needed.
        enc_type = strtoenc(blobmsg_data(tb[CONNECT_ENC_TYPE]));
        if (enc_type == ENC_TYPE_UNKNOWN) {
            beep_reply_error(ctx, req, "invalid enc_type",
                    BEEP_UBUS_ERROR_ARGS);
            return 0;
        } else if (enc_type != ENC_TYPE_NONE) {
            if (!tb[CONNECT_KEY]) {
                beep_reply_error(ctx, req, "missing key",
                        BEEP_UBUS_ERROR_ARGS);
                return 0;
            }
        }
    }

    if (!tb[CONNECT_RETURN_ON]) {
        beep_reply_error(ctx, req, "missing return_on", BEEP_UBUS_ERROR_ARGS);
        return 0;
    } else {
        // Check that return_on is valid.
        return_on = strtoconnret(blobmsg_data(tb[CONNECT_RETURN_ON]));
        if (return_on == CONN_RETURN_ON_UNKNOWN) {
            beep_reply_error(ctx, req, "invalid return_on",
                    BEEP_UBUS_ERROR_ARGS);
            return 0;
        }
    }

    if (!tb[CONNECT_SHUTDOWN]) {
        beep_reply_error(ctx, req, "missing shutdown", BEEP_UBUS_ERROR_ARGS);
        return 0;
    }

    params = (struct ConnectParams *)malloc(sizeof(struct ConnectParams));
    if (!params) {
        LOG_ERROR(log_beep_main, "out of memory");
        wsd_shutdown_state = WSD_SHUTDOWN_STATE_STOPPING;
        return 0;
    }
    memset(params, 0, sizeof(struct ConnectParams));

    params->ap.essid = strdup(blobmsg_data(tb[CONNECT_ESSID]));
    params->ap.enc_type = enc_type;

    // Already checked if we need the key or not.
    if (tb[CONNECT_KEY]) {
        params->key = strdup(blobmsg_data(tb[CONNECT_KEY]));
    }

    params->return_on = return_on;
    params->shutdown_on_success = (bool)blobmsg_get_u32(tb[CONNECT_SHUTDOWN]);

    // Optional arguments.
    if (tb[CONNECT_BSSID]) {
        if(strtobssid(blobmsg_data(tb[CONNECT_BSSID]), params->ap.bssid)) {
            // If we get an error just clear the bssid.
            memset(params->ap.bssid, 0, 6);
        }
    }

    // Convert confirm time into uloop ticks.
    if (tb[CONNECT_CONFIRM_TIME]) {
        params->confirm_counter = blobmsg_get_u32(tb[CONNECT_CONFIRM_TIME])
                / WSD_BG_ULOOP_TIMER;
    }
    if (params->confirm_counter <= 0) {
        params->confirm_counter = DEFAULT_CONNECT_CONFIRM_TIME
                / WSD_BG_ULOOP_TIMER;
    }

    // Set this last, tells the wsd_on_interval a connection has
    // been requested.
    conn_params = params;

    beep_reply_success(ctx, req, NULL);
    return 0;
}

static void free_conn_params(void) {
    if (conn_params) {
        if (conn_params->ap.essid)
            free(conn_params->ap.essid);
        if (conn_params->key)
            free(conn_params->key);
        free(conn_params);
        conn_params = NULL;
    }
}

static const struct ubus_method wsd_methods[] = {
    UBUS_METHOD("shutdown", wsd_shutdown, shutdown_policy),
    UBUS_METHOD_NOARG("status", wsd_status),
    UBUS_METHOD("rename", wsd_rename, rename_policy),
    UBUS_METHOD("connect", wsd_connect, connect_policy)
};

static struct ubus_object_type wsd_object_type =
    UBUS_OBJECT_TYPE("beep.wifisetup", wsd_methods);

static struct ubus_object wsd_object = {
    .name = "beep.wifisetup",
    .type = &wsd_object_type,
    .methods = wsd_methods,
    .n_methods = ARRAY_SIZE(wsd_methods),
};

static void wsd_on_interval(struct uloop_timeout *t);
static struct uloop_timeout periodic_timeout = {
    .cb = wsd_on_interval
};

static inline void wsd_bg_trigger(void) {
    pthread_cond_signal(&wsd_bg_cond);
    pthread_mutex_unlock(&wsd_bg_mutex);
}

static void wsd_on_interval(struct uloop_timeout *t) {
    if (wsd_shutdown_state == WSD_SHUTDOWN_STATE_STOPPING) {
        // If the bg thread state is not stopping it may need to be
        // unlocked one last time.
        if (wsd_bg_state != WSD_BG_STATE_STOPPING) {
            // Set the bg state to stopping in case the thread
            // is busy and knows not to reset back to the idle state.
            wsd_bg_state = WSD_BG_STATE_STOPPING;
            LOG_DEBUG(log_beep_main, "stopping wifisetup daemon");
            wsd_bg_trigger();
        }
        uloop_timeout_set(&periodic_timeout, WSD_BG_ULOOP_TIMER);
        return;
    } else if (wsd_shutdown_state == WSD_SHUTDOWN_STATE_JOINING) {
        LOG_DEBUG(log_beep_main, "sending shutdown evt");
        ws_shutdown_evt();
        ubus_remove_object(ubus_ctx, &wsd_object);
        uloop_end();
        return;
    }

    if (scan_counter > 0)
        scan_counter--;

    if (wsd_bg_state == WSD_BG_STATE_CONFIRM_WAIT) {
        conn_params->confirm_counter--;

        if (!conn_params->confirm_counter) {
            // Confirm timed out, set reason, clear the connect request and
            // go back to idle.
            pthread_mutex_lock(&wsd_data_lock);
            if (conn_reason_str)
                free(conn_reason_str);
            conn_reason = WSD_BG_CONNECT_CONFIRM_TIMEOUT;
            conn_reason_str = strdup("confirm timeout");
            pthread_mutex_unlock(&wsd_data_lock);

            free_conn_params();
            wsd_bg_state = WSD_BG_STATE_IDLE;
        }
    } else if (wsd_bg_state == WSD_BG_STATE_IDLE) {
        // Connecting takes precedent.
        if (conn_params) {
            wsd_bg_state = WSD_BG_STATE_CONNECT;
            wsd_bg_trigger();
        } else if (scan_counter <= 0) {
            if(startup_fast_scan_counter > 0) {
                LOG_INFO(log_beep_main, ">>> FAST SCAN <<<");
                startup_fast_scan_counter--;
                scan_counter = WIFI_FAST_SCAN_MS_PERIOD / WSD_BG_ULOOP_TIMER;
            } else {
                scan_counter = WIFI_SCAN_MS_PERIOD / WSD_BG_ULOOP_TIMER;
            }
            LOG_INFO(log_beep_main, "Next scan in %d iterations", scan_counter);
            wsd_bg_state = WSD_BG_STATE_SCAN;
            wsd_bg_trigger();
        }
    }

    uloop_timeout_set(&periodic_timeout, WSD_BG_ULOOP_TIMER);
}

/*
 * For each AP <x> in list_A:
 *     If <x> is not in list_B, increment <x>.miss_counter by 1, and
 *         if <x>.miss_counter = 3, remove from list_A
 *     (The intersection case, <x> in list_A and list_B is handled in next loop)
 *
 * For each AP <y> in list_B:
 *     If <y> is in list_A, reset <y>.miss_counter to 0 and update essid,
 *         enc_type, signal and channel.
 *     If <y> is not in list_A, add <y> to list_A and set miss_counter to 0
 */

static AP *find_bssid_in_list(uint8_t *bssid, AP *list_head) {
    if(!bssid || !list_head) {
        return NULL;
    }

    while(list_head) {
        bool match = true;
        for(int i=0;i<6;i++) {
            if(list_head->bssid[i] != bssid[i]) {
                match = false;
                break;
            }
        }

        if(match) {
            return list_head;
        }

        list_head = list_head->next;
    }

    return NULL;
}

// This is a NOOP if ap is not in list.
static AP *remove_ap_from_list(AP *ap, AP *list) {
    if(!ap || !list) {
        return list;
    }

    AP *head = list;
    AP *prev = NULL;

    while(head && head != ap) {
        prev = head;
        head = head->next;
    }

    if(!head) {// Didn't find ap in list
        return list;
    }

    // invariant here: head == ap

    if(prev) { // AP not first item
        prev->next = head->next;
    } else { // First item was ap, list_head is changing
        list = head->next;
    }

    ap->next = NULL;

    return list;
}

static AP *add_ap_to_list(AP *ap, AP *list) {
    AP *head = list;
    if(!ap) {
        return list;
    }

    while(head && head->next) {
        head = head->next;
    }

    ap->next = NULL;

    if(head) {
        head->next = ap;
    } else {
        list = ap;
    }

    return list;
}

static AP *update_scan_ap_lists(AP *list_a, AP *list_b) {
    AP *head, *next, *target;
    uint8_t *bssid;

    // Fail fast if list_b is null (this might happen is ws_netbe_scan fails)
    if(!list_b) {
        return list_a;
    }

    head = list_a;
    while(head) {
        bssid = head->bssid;
        next = head->next;
        if(!find_bssid_in_list(bssid, list_b)) {
            if(++head->miss_counter >= 3) {
                LOG_DEBUG(log_beep_main, "'%s' REMOVED",
                        head->essid);
                list_a = remove_ap_from_list(head, list_a);
                ws_ap_head_free(head);
            } else {
                LOG_DEBUG(log_beep_main, "'%s' miss #%d",
                        head->essid, head->miss_counter);
            }
        }
        head = next;
    }

    head = list_b;
    while(head) {
        bssid = head->bssid;
        if((target = find_bssid_in_list(bssid, list_a))) {
            LOG_DEBUG(log_beep_main, "'%s' UPDATED",
                    head->essid);
            if(target->essid) {
                free(target->essid);
            }
            if (head->essid) {
                target->essid = strdup(head->essid);
            } else {
                target->essid = NULL;
            }
            target->enc_type = head->enc_type;
            target->signal = head->signal;
            target->channel = head->channel;
            target->miss_counter = 0;
        } else {
            LOG_DEBUG(log_beep_main, "'%s' ADDED",
                    head->essid);
            list_a = add_ap_to_list(ws_ap_copy(head), list_a);
        }
        head = head->next;
    }

    return list_a;
}

void *wsd_bg(void *arg) {
    int ret;
    LOG_DEBUG(log_beep_main, "background thread started");

    LOG_DEBUG(log_beep_main, "switching to setup mode");
    ret = ws_netbe_set_mode(BE_MODE_AP_SETUP);
    if (ret) {
        LOG_ERROR(log_beep_main, "error starting setup mode");
        wsd_bg_state = WSD_BG_STATE_ERROR;
        wsd_shutdown_state = WSD_SHUTDOWN_STATE_JOINING;
        return NULL;
    }

    wsd_bg_state = WSD_BG_STATE_IDLE;
    while (!wsd_shutdown_state) {
        // Wait for next command.
        pthread_cond_wait(&wsd_bg_cond, &wsd_bg_mutex);
        if (wsd_shutdown_state)
            break;

        LOG_INFO(log_beep_main, "ev fired");

        switch (wsd_bg_state) {
        case WSD_BG_STATE_SCAN: {
            AP *new_scan_ap_head;
            OPMode new_opmode;

            LOG_INFO(log_beep_main, "scan");
            new_scan_ap_head = ws_netbe_scan();
            // Get the opmode at the same time since we should be calling
            // into the network libraries from a single thread.
            new_opmode = ws_netbe_op_mode();

            pthread_mutex_lock(&wsd_data_lock);
            scan_ap_head = update_scan_ap_lists(scan_ap_head, new_scan_ap_head);
            opmode = new_opmode;
            pthread_mutex_unlock(&wsd_data_lock);

            ws_ap_head_free(new_scan_ap_head);

            if (wsd_bg_state != WSD_BG_STATE_STOPPING)
                wsd_bg_state = WSD_BG_STATE_IDLE;
            break;
        }

        case WSD_BG_STATE_CONNECT: {
            int new_conn_reason;
            char *new_conn_reason_str;

            LOG_INFO(log_beep_main, "connect");
            // check_connect = conn_params->return_on
            ret = ws_netbe_connect(&conn_params->ap, conn_params->key,
                    conn_params->return_on, &new_conn_reason,
                    &new_conn_reason_str);

            // Don't care about if the connection was successful or not, so
            // prepare to exit.
            if (conn_params->return_on == CONN_RETURN_ON_NEVER) {
                wsd_shutdown_state = WSD_SHUTDOWN_STATE_STOPPING;
                // ret will only be non-zero if there was an internal error.
                // In that case don't save on exit to be safe.
                wsd_save_on_exit = (ret == 0);
                // Don't need to reset wsd_bg_state.
                break;
            }

            // Save connect results.
            pthread_mutex_lock(&wsd_data_lock);
            if (conn_reason_str)
                free(conn_reason_str);

            conn_sec = (uint32_t)(beep_millis() / 1000);

            if (ret) {
                conn_reason = WSD_BG_CONNECT_INTERNAL_ERROR;
                conn_reason_str = strdup("internal error");
            } else {
                conn_reason = new_conn_reason;
                conn_reason_str = new_conn_reason_str;
            }
            pthread_mutex_unlock(&wsd_data_lock);

            if (conn_params->return_on == CONN_RETURN_ON_ALWAYS
                    || conn_reason != 0) {
                // In all error cases or ON_ALWAYS, clear the connect
                // request and go back to idle.
                free_conn_params();
                if (wsd_bg_state != WSD_BG_STATE_STOPPING)
                    wsd_bg_state = WSD_BG_STATE_IDLE;
            } else if (conn_params->return_on
                    == CONN_RETURN_ON_CONNECT_ERROR) {
                // If the connect shutdown argument is 1, prepare to exit
                // and save.  If it is 0 stay connected to the network but
                // clear the connection request and go back to idle.
                if (conn_params->shutdown_on_success) {
                    wsd_shutdown_state = WSD_SHUTDOWN_STATE_STOPPING;
                    wsd_save_on_exit = true;
                } else {
                    free_conn_params();
                    if (wsd_bg_state != WSD_BG_STATE_STOPPING)
                        wsd_bg_state = WSD_BG_STATE_IDLE;
                }
            } else if (conn_params->return_on
                    == CONN_RETURN_ON_CONFIRM_ERROR) {
                // Start confirm countdown, don't clear connect_request.
                if (wsd_bg_state != WSD_BG_STATE_STOPPING)
                    wsd_bg_state = WSD_BG_STATE_CONFIRM_WAIT;
            } else {
                LOG_ERROR(log_beep_main, "invalid state: %d %d",
                        conn_params->return_on, conn_reason);
                free_conn_params();
                if (wsd_bg_state != WSD_BG_STATE_STOPPING)
                    wsd_bg_state = WSD_BG_STATE_IDLE;
            }

            break;
        }

        default:
            LOG_ERROR(log_beep_main, "triggered on invalid state: %d",
                    wsd_bg_state);
            abort();
            break;
        }
    }

    wsd_bg_state = WSD_BG_STATE_STOPPING;

    if (!wsd_save_on_exit) {
        ret = ws_netbe_revert();
        if (ret) {
            LOG_ERROR(log_beep_main, "error reverting uci changes: %d", ret);
        }
    }

    ret = ws_netbe_set_mode(BE_MODE_CLIENT);
    if (ret) {
        LOG_ERROR(log_beep_main, "error starting client mode");
        wsd_bg_state = WSD_BG_STATE_ERROR;
    }

    ret = ws_netbe_commit();
    if (ret) {
        LOG_ERROR(log_beep_main, "error saving uci changes: %d", ret);
    }

    LOG_DEBUG(log_beep_main, "background thread joining");

    wsd_shutdown_state = WSD_SHUTDOWN_STATE_JOINING;

    return NULL;
}

int wsd_start(void) {
    int ret = 0;

    LOG_DEBUG(log_beep_main, "starting wifisetup daemon");

    scan_counter = WIFI_SCAN_NOW;
    startup_fast_scan_counter = WIFI_FAST_SCAN_ITERATIONS - 1;
    wsd_bg_state = WSD_BG_STATE_STARTING;
    conn_reason = WSD_BG_CONNECT_NA;
    conn_reason_str = strdup("na");

    scan_ap_head = NULL;
    opmode = OP_MODE_UNKNOWN;

    pthread_mutex_init(&wsd_bg_mutex, NULL);
    pthread_mutex_init(&wsd_data_lock, NULL);
    pthread_cond_init(&wsd_bg_cond, NULL);
    pthread_mutex_lock(&wsd_bg_mutex);

    pthread_create(&wsd_bg_thread, NULL, wsd_bg, NULL);

    if ((ret = ubus_add_object(ubus_ctx, &wsd_object)))
        return ret;

    uloop_timeout_set(&periodic_timeout, WSD_BG_ULOOP_TIMER);

    uloop_run();

    pthread_join(wsd_bg_thread, NULL);
    LOG_DEBUG(log_beep_main, "thread joined");

    // Make valgrind happy.
    free_conn_params();
    if (scan_ap_head) {
        ws_ap_head_free(scan_ap_head);
    }
    if (conn_reason_str) {
        free(conn_reason_str);
    }

    return ret;
}
