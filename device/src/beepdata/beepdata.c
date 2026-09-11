/* This component is used for storing and retrieving played station info.
 *
 * Might be nice to implement it as another process but we can't currently
 * route network commands from beepmusicd to other processes.
 * */

#include <json.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "beep/beep_ubus.h"
#include "beep/debug.h"
#include "beep/flags.h"

#include "model.h"

///// Flags

struct flag_vals {
    char* data_path;
    // The amount of time (in seconds) a station must be played to be
    // considered to become a lucky station.
    int lucky_duration;
};

#define BEEP_LUCKY_DURATION_DEFAULT 0
struct flag_vals beepdata_flags = {
    "/beep/beepdata.db",
    BEEP_LUCKY_DURATION_DEFAULT};

static const BeepFlag flags[] = {
    BEEP_FLAG("data_path", BEEP_FLAG_STRING, &beepdata_flags.data_path, NULL, NULL),
    BEEP_FLAG("lucky_duration", BEEP_FLAG_INT, &beepdata_flags.lucky_duration, NULL, NULL)
};

BEEP_INIT_FLAGS(flags);

///// End Flags

static struct ubus_context *ctx;
static struct ubus_event_handler listener;
static struct blob_buf b;

static int next_lucky_stations(
        struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    blob_buf_init(&b, 0);
    json_object* json_response = json_object_new_object();

    QUEUE lucky_stations = beep_model_history_query_lucky();
    json_object* stations = json_object_new_array();
    qForEach(BeepModelStationSpan*, span, lucky_stations) {
        json_object_array_add(stations, span->station);
    }
    json_object_object_add(json_response, "stations", stations);
    beep_model_span_queue_destroy(lucky_stations);

    blobmsg_add_object(&b, json_response);
    beep_reply_success(ctx, req, b.head);
    json_object_put(json_response);

    return 0;
}

static const struct ubus_method beepdata_methods[] = {
    UBUS_METHOD_NOARG("next_lucky_stations", next_lucky_stations),
};

static struct ubus_object_type beepdata_object_type =
    UBUS_OBJECT_TYPE("beep.data", beepdata_methods);

static struct ubus_object beepdata_object = {
    .name = "beep.data",
    .type = &beepdata_object_type,
    .methods = beepdata_methods,
    .n_methods = ARRAY_SIZE(beepdata_methods),
};

void handle_update(struct blob_attr* msg, const char* event_type) {
    if (!strcmp(event_type, "station_changed")) {
        LOG_INFO(log_beep_main, "Got station change");
        struct blob_attr *state[__DISTRIBUTOR_STATE_MAX];
        blobmsg_parse(distributor_state_policy, __DISTRIBUTOR_STATE_MAX,
                state, blobmsg_data(msg), blob_len(msg));

        if (!state[DISTRIBUTOR_STATE_STATION]) {
            LOG_ERROR(log_beep_main, "Received invalid distributor state");
            return;
        }

        char* json_station_string = blobmsg_format_json(
                state[DISTRIBUTOR_STATE_STATION],
                false);
        LOG_INFO(log_beep_main, "json_station_string: %s", json_station_string);
        // blobmsg_format_json can only parse a blob_attr which always
        // includes a key. We want to skip the key, so we start from the first
        // '{'.
        char* json_station_obj_string = strchr(json_station_string, '{');
        if (!json_station_obj_string) {
            LOG_ERROR(log_beep_main, "Could not parse station");
            return;
        }
        json_object* json_station = json_tokener_parse(json_station_obj_string);

        beep_model_history_add(
                BEEP_MODEL_HISTORY_EVENT_STATION_CHANGE,
                json_station);

        json_object_put(json_station);
        free(json_station_string);
    }
}

void on_distributor_event(
        struct ubus_context *ctx, struct ubus_event_handler *ev,
        const char* type, struct blob_attr* msg) {
    struct blob_attr *tb[__BEEP_STATE_MAX];
    blobmsg_parse(beep_state_policy, __BEEP_STATE_MAX,
            tb, blob_data(msg), blob_len(msg));
    handle_update(tb[BEEP_STATE_STATE],
            blobmsg_get_string(tb[BEEP_STATE_EVENT_TYPE]));
}

struct uloop_timeout heartbeat_timeout;

void on_heartbeat_timeout(struct uloop_timeout *t) {
    LOG_INFO(log_beep_main, "heartbeat");
    beep_model_history_write_heartbeat();

    // every 9 minutes
    uloop_timeout_set(&heartbeat_timeout, 9 * 60 * 1000);
}

struct uloop_timeout heartbeat_timeout = {
    .cb = on_heartbeat_timeout
};

int main(int argc, char *argv[])
{
    log_beep_main = LOG_CATEGORY_GET("beepdata");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    beep_flags_init(argc, argv);

    uloop_init();

    ctx = beep_ubus_connect("beepdata");
    if(!ctx) {
        LOG_ERROR(log_beep_main, "Failed to connect to ubus");
        return 1;
    }

    ubus_add_uloop(ctx);

    int ret = ubus_add_object(ctx, &beepdata_object);
    if (ret) {
        LOG_ERROR(log_beep_main,
                "Failed to add object: %s", ubus_strerror(ret));
        exit(1);
    }

    beep_model_history_init(
            beepdata_flags.data_path, beepdata_flags.lucky_duration);

    // TODO: log an error if a station is already playing when we start

    listener.cb = on_distributor_event;
    ubus_register_event_handler(
            ctx, &listener, "beep.state.distributor._local_");

    uloop_timeout_set(&heartbeat_timeout, 1);

    uloop_run();

    uloop_done();

    return 0;
}
