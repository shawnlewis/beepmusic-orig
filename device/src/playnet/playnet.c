#define _GNU_SOURCE

#include <getopt.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <libubox/blobmsg_json.h>
#include <libubus.h>

#include "beep/debug.h"
#include "beep/beep_stream_ch.h"
#include "beep/beep_ubus.h"
#include "beep/config.h"
#include "beep/flags.h"
#include "beep/net.h"
#include "beep/player.h"

#define PLAYNET_SYNC_MTU 16384

#define BEEP_PLAYNET_STREAM_PORT 32299
#define BEEP_PLAYNET_DEFAULT_GAIN 14155  // 60% of volume (not gain)
#define BEEP_PLAYNET_GAIN_PERSIST_DELAY 10000

///// Flags

// This is an app-wide global, available via debug.h
struct flag_vals {
    int port_base;
    char* name;
    char* conf_dir;
};

// With default values;
struct flag_vals playnet_flags = {
    .port_base = -1,
};

static const BeepFlag flags[] = {
    BEEP_FLAG("port_base", BEEP_FLAG_INT, &playnet_flags.port_base, NULL, NULL),
    BEEP_FLAG("conf_dir", BEEP_FLAG_STRING, &playnet_flags.conf_dir, NULL, NULL),
    BEEP_FLAG("name", BEEP_FLAG_STRING, &playnet_flags.name, NULL, NULL)
};

BEEP_INIT_FLAGS(flags);

///// End Flags


BeepPlayer* player;
static bool playnet_shutdown_wait = false;

static int stream_port = BEEP_PLAYNET_STREAM_PORT;

static struct ubus_context *ctx;
static struct blob_buf b;

static bool is_paused = true;


void add_status_to_blob(struct blob_buf* buf, const BeepPlayerStatus* status) {
    blobmsg_add_u32(&b, "num_tracks_started", status->num_tracks_started);
    blobmsg_add_u32(&b, "output_used", status->output_used);
    blobmsg_add_u32(&b, "output_size", status->output_size);
    blobmsg_add_u32(&b, "streambuf_free",
            status->streambuf_size - status->streambuf_used);
    blobmsg_add_u8(&b, "can_st_begin", status->can_prepare_decoder);
    blobmsg_add_u32(&b, "written_track_time", status->written_track_time);

    blobmsg_add_u32(&b, "sync_played_time", status->sync_played_time);
    blobmsg_add_u32(&b, "sync_timestamp", status->sync_timestamp);
    blobmsg_add_u32(&b, "gain", status->gain);
    blobmsg_add_u32(&b, "bitrate", status->bitrate);
    blobmsg_add_u32(&b, "cookie", status->play_cookie);

    blobmsg_add_u32(&b, "paused", is_paused);
}

void trigger_update(const char* event_type) {
    BeepPlayerStatus status = player->status(player);

    static struct blob_buf event_data;
    blob_buf_init(&event_data, 0);

    blob_buf_init(&b, 0);
    add_status_to_blob(&b, &status);

    beep_send_state(ctx, "playnet", event_type, event_data.head, b.head);
}

static int playnet_hello(struct ubus_context *ctx, struct ubus_object *obj,
              struct ubus_request_data *req, const char *method,
              struct blob_attr *msg) {
    blob_buf_init(&b, 0);
    blobmsg_add_u32(&b, "stream_port", stream_port);

    void* cookie = blobmsg_open_table(&b, "status");
    BeepPlayerStatus status = player->status(player);
    add_status_to_blob(&b, &status);
    blobmsg_close_table(&b, cookie);

    beep_reply_success(ctx, req, b.head);
    return 0;
}

enum {
    RESUME_TIME_H,
    RESUME_TIME_L,
    __RESUME_MAX
};

static const struct blobmsg_policy resume_policy[] = {
    [RESUME_TIME_H] = { .name = "timeh", .type = BLOBMSG_TYPE_INT32 },
    [RESUME_TIME_L] = { .name = "timel", .type = BLOBMSG_TYPE_INT32 },
};

static int playnet_resume(struct ubus_context *ctx, struct ubus_object *obj,
              struct ubus_request_data *req, const char *method,
              struct blob_attr *msg) {
    struct blob_attr *tb[__RESUME_MAX];
    uint64_t time;

    is_paused = false;
    trigger_update("resumed");

    blobmsg_parse(
            resume_policy, ARRAY_SIZE(resume_policy),
            tb, blob_data(msg), blob_len(msg));

    if (!tb[RESUME_TIME_L]) {
        time = 0;
    } else {
        uint32_t timeh = blobmsg_get_u32(tb[RESUME_TIME_H]);
        uint32_t timel = blobmsg_get_u32(tb[RESUME_TIME_L]);
        time = ((uint64_t) timeh << 32) + timel;
    }

    player->resume(player, time);

    beep_reply_success(ctx, req, NULL);

    return 0;
}

enum {
    SKIP_AHEAD_INTERVAL,
    __SKIP_AHEAD_MAX
};

static const struct blobmsg_policy skip_ahead_policy[] = {
    [SKIP_AHEAD_INTERVAL] = { .name = "interval", .type = BLOBMSG_TYPE_INT32 },
};

static int playnet_skip_ahead(
              struct ubus_context *ctx, struct ubus_object *obj,
              struct ubus_request_data *req, const char *method,
              struct blob_attr *msg) {
    struct blob_attr *tb[__SKIP_AHEAD_MAX];

    blobmsg_parse(
            skip_ahead_policy, ARRAY_SIZE(skip_ahead_policy),
            tb, blob_data(msg), blob_len(msg));

    if (!tb[SKIP_AHEAD_INTERVAL]) {
        beep_reply_error(ctx, req, "Missing interval", 0);
        return 0;
    }
    uint32_t interval = blobmsg_get_u32(tb[SKIP_AHEAD_INTERVAL]);

    player->skip_ahead(player, interval);

    beep_reply_success(ctx, req, NULL);

    return 0;
}

static int playnet_pause(struct ubus_context *ctx, struct ubus_object *obj,
              struct ubus_request_data *req, const char *method,
              struct blob_attr *msg) {
    is_paused = true;
    trigger_update("paused");

    player->pause(player);

    beep_reply_success(ctx, req, NULL);

    return 0;
}

enum {
    STOP_SET_COOKIE,
    __STOP_MAX
};

static const struct blobmsg_policy stop_policy[] = {
    [STOP_SET_COOKIE] = { .name = "set_cookie", .type = BLOBMSG_TYPE_INT32 },
};

static int playnet_stop(struct ubus_context *ctx, struct ubus_object *obj,
              struct ubus_request_data *req, const char *method,
              struct blob_attr *msg) {
    struct blob_attr *tb[__STOP_MAX];

    blobmsg_parse(
            stop_policy, ARRAY_SIZE(stop_policy),
            tb, blob_data(msg), blob_len(msg));

    if (!tb[STOP_SET_COOKIE]) {
        beep_reply_error(ctx, req, "Missing set_cookie", 0);
        return 0;
    }

    player->stop(player, blobmsg_get_u32(tb[STOP_SET_COOKIE]));

    beep_reply_success(ctx, req, NULL);

    return 0;
}

enum {
    SET_VOLUME_GAIN,
    __SET_VOLUME_MAX
};

static const struct blobmsg_policy set_volume_policy[] = {
    [SET_VOLUME_GAIN] = { .name = "gain", .type = BLOBMSG_TYPE_INT32 },
};

static void reset_gain_persist_timeout(void);

static int playnet_set_volume(
        struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    struct blob_attr *tb[__SET_VOLUME_MAX];

    blobmsg_parse(
            set_volume_policy, ARRAY_SIZE(set_volume_policy),
            tb, blob_data(msg), blob_len(msg));

    if (!tb[SET_VOLUME_GAIN]) {
        beep_reply_error(ctx, req, "Missing gain argument", 0);
        return 0;
    }
    uint32_t gain = blobmsg_get_u32(tb[SET_VOLUME_GAIN]);
    if (gain > (1 << 16)) {
        beep_reply_error(ctx, req, "Gain must be < 65536", 0);
        return 0;
    }

    BeepPlayerStatus status = player->status(player);

    if (status.gain != gain) {
        player->set_volume(player, gain);
        reset_gain_persist_timeout();
    }

    beep_reply_success(ctx, req, NULL);

    return 0;
}

enum {
    ADJUST_VOLUME_DELTA,
    __ADJUST_VOLUME_MAX
};

static const struct blobmsg_policy adjust_volume_policy[] = {
    [ADJUST_VOLUME_DELTA] = { .name = "delta", .type = BLOBMSG_TYPE_INT32 },
};

static int playnet_adjust_volume(
        struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    struct blob_attr *tb[__ADJUST_VOLUME_MAX];

    blobmsg_parse(
            adjust_volume_policy, ARRAY_SIZE(adjust_volume_policy),
            tb, blob_data(msg), blob_len(msg));

    if(!tb[ADJUST_VOLUME_DELTA]) {
        beep_reply_error(ctx, req, "Missing vol. delta argument", 0);
        return 0;
    }
    uint32_t gain = blobmsg_get_u32(tb[ADJUST_VOLUME_DELTA]);

    player->adjust_volume(player, gain);
    reset_gain_persist_timeout();

    beep_reply_success(ctx, req, NULL);

    return 0;
}

static int playnet_shutdown(struct ubus_context *ctx, struct ubus_object *obj,
              struct ubus_request_data *req, const char *method,
              struct blob_attr *msg) {
    beep_reply_success(ctx, req, NULL);
    playnet_shutdown_wait = true;
    player->shutdown(player);
    return 0;
}

static int playnet_get_state(struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    BeepPlayerStatus status = player->status(player);
    blob_buf_init(&b, 0);
    add_status_to_blob(&b, &status);
    beep_reply_success(ctx, req, b.head);
    return 0;
}

static const struct ubus_method playnet_methods[] = {
    UBUS_METHOD_NOARG("hello", playnet_hello),
    UBUS_METHOD("resume", playnet_resume, resume_policy),
    UBUS_METHOD_NOARG("pause", playnet_pause),
    UBUS_METHOD("stop", playnet_stop, stop_policy),
    UBUS_METHOD("skip_ahead", playnet_skip_ahead, skip_ahead_policy),
    UBUS_METHOD("set_volume", playnet_set_volume, set_volume_policy),
    UBUS_METHOD("adjust_volume", playnet_adjust_volume, adjust_volume_policy),
    UBUS_METHOD_NOARG("shutdown", playnet_shutdown),
    UBUS_METHOD_NOARG("get_state", playnet_get_state),
};

static struct ubus_object_type playnet_object_type =
    UBUS_OBJECT_TYPE("beep.playnet", playnet_methods);

static struct ubus_object playnet_object = {
    .name = "beep.playnet",
    .type = &playnet_object_type,
    .methods = playnet_methods,
    .n_methods = ARRAY_SIZE(playnet_methods),
};

static struct uloop_timeout gain_persist_timeout;

void on_gain_persist(struct uloop_timeout *t) {
    char *gain_string;

    if(playnet_shutdown_wait) {
        return;
    }

    BeepPlayerStatus status = player->status(player);
    if(asprintf(&gain_string, "%d", status.gain) <= 0) {
        LOG_WARN(log_beep_main, "Can't allocate memory for gain string");
        return;
    }

    if(!beep_config_data_write("playnet_gain", gain_string)) {
        LOG_WARN(log_beep_main, "Couldn't persist local gain");
    } else {
        LOG_INFO(log_beep_main, "Local gain %d saved", status.gain);
    }

    free(gain_string);
}

static struct uloop_timeout gain_persist_timeout = {
    .cb = on_gain_persist
};

static void reset_gain_persist_timeout(void) {
    uloop_timeout_set(&gain_persist_timeout, BEEP_PLAYNET_GAIN_PERSIST_DELAY);
}

int playnet_on_start(BeepStreamChStart *data) {
    // TODO: Create some sort of return code enum. (beep_spec.h)
    return (player->start(player) == BEEP_PLAYER_OK) ? 1 : 0;
}

int playnet_on_st_end(BeepStreamChStEnd *data) {
    return (player->st_end(player) == BEEP_PLAYER_OK) ? 1 : 0;
}

int playnet_on_sync_pt(BeepStreamChSyncPt *data) {
    return 1;
}

int playnet_on_buffer(BeepStreamChBuffer *data, uint8_t *payload, int size) {
    // TODO: Create some sort of return code enum. (beep_spec.h)
    // And unify with the packet return codes.  Don't think this is the best
    // place to put the stop check but we can't send that information to the
    // source.
    int ret = player->buffer_blocking(player, payload, size);
    if (ret == BEEP_PLAYER_STOPPED || ret == BEEP_PLAYER_OK) {
        return 1;
    }
    return 0;
}

int playnet_on_st_begin(BeepStreamChStBegin *data) {
    char audio_type = data->type;
    return (player->st_begin_blocking(
                player,
                audio_type,
                data->transition_type,
                data->transition_period,
                data->replay_gain,
                data->output_threshold,
                data->polarity_inversion,
                data->output_channels,
                NULL) ==
        BEEP_PLAYER_OK) ? 1 : 0;
}

int playnet_on_flush(BeepStreamChFlush *data) {
    fprintf(stderr, "FLUSH\n");
    player->flush(player, data->set_cookie);
    return 1;
}

int playnet_on_sync_data(BeepStreamChSyncData *data, uint8_t *payload, int size) {
    int ret = beep_player_local_sync_state_recv(player, payload, size);
    if (ret == BEEP_PLAYER_OK) {
        LOG_DEBUG(log_beep_main, "sync done, resume NOW.");
        player->resume(player, 0);
        beep_player_local_prepare_sync_wait(player, false);
    }
    if (ret == BEEP_PLAYER_AGAIN || ret == BEEP_PLAYER_OK) {
        return 1;
    }
    return 0;
}

int playnet_on_sync_to(BeepStreamChSyncTo *data, uint8_t *payload, int size) {
    return 0;
//
//    BeepStreamChCtx sync_to_st_ctx;
//    uint32_t len;
//    int port, stat, ret = 0, counter = 0;
//    uint8_t buf[PLAYNET_SYNC_MTU];
//    char host[24];
//
//    if ((size_t)size > sizeof(host)) {
//        LOG_ERROR(log_beep_main, "host string too long");
//        return 0;
//    }
//
//    strncpy(host, (char *)payload, sizeof(host));
//
//    port = split_host_port(host);
//    LOG_DEBUG(log_beep_main, "attempt to sync to: %s:%d", host, port);
//    if (stream_ch_connect(&sync_to_st_ctx, host, port + 1) == 0) {
//        LOG_ERROR(log_beep_main, "could not connect to sync receiver");
//        return 0;
//    }
//
//    if (stream_ch_start(&sync_to_st_ctx, NULL) == 0 ||
//        // TODO: mp3 audio_type is hard-coded.
//        stream_ch_st_begin(&sync_to_st_ctx, 'm', 0, 0, 0, 0, 0, 0) == 0) {
//        LOG_ERROR(log_beep_main, "sync receiver could not start");
//        goto playnet_sync_to_done;
//    }
//
//    // TODO: This should be moved to playfile/group later.
//    if (stream_ch_sync_wait(&sync_to_st_ctx) == 0) {
//        LOG_ERROR(log_beep_main, "sync receiver could not sync wait");
//        goto playnet_sync_to_done;
//    }
//
//    stat = beep_player_local_prepare_sync_to(player, true);
//    while (stat == BEEP_PLAYER_AGAIN || stat == BEEP_PLAYER_OK) {
//        len = PLAYNET_SYNC_MTU;
//        stat = beep_player_local_sync_state_send(player, buf, &len);
//        if (stream_ch_sync_data(&sync_to_st_ctx, buf, len) == 0) {
//            LOG_ERROR(log_beep_main, "sending sync data");
//            break;
//        }
//        counter += len;
//        if (stat == BEEP_PLAYER_OK) {
//            ret = 1;  // send all data.
//        }
//        if (stat != BEEP_PLAYER_AGAIN) {
//            break;
//        }
//    }
//    beep_player_local_prepare_sync_to(player, false);
//    LOG_DEBUG(log_beep_main, "sync done sent %d bytes", counter);
//
//playnet_sync_to_done:
//    stream_ch_disconnect(&sync_to_st_ctx);
//    return ret;
}

int playnet_on_sync_wait(void) {
    // hardcoded wait to allow start/st_begin commands to execute since
    // beep_player_local_prepare_sync_wait will flush them out of streambuf.
    usleep(1000000);
    return (beep_player_local_prepare_sync_wait(player, true) ==
        BEEP_PLAYER_OK) ? 1 : 0;
}

static const BeepStreamChEvents playnet_stream_events = {
    playnet_on_start,
    playnet_on_st_end,
    playnet_on_sync_pt,
    playnet_on_buffer,
    playnet_on_st_begin,
    playnet_on_flush,
    playnet_on_sync_data,
    playnet_on_sync_to,
    playnet_on_sync_wait
};

void song_started_cb(void* user_data, void* song_data) {
    LOG_DEBUG(log_beep_main, "Song Started");
}

int main(int argc, char** argv) {
    BeepStreamChCtx stream_ctx;

    log_beep_main = LOG_CATEGORY_GET("playnet");
    log_category_set_priority (log_beep_main, LOG_PRIORITY_DEBUG);

    beep_flags_init(argc, argv);

    // TODO: Really shouldn\'t use fixed ports for these.
    if (playnet_flags.port_base != -1) {
        stream_port = playnet_flags.port_base;
    }

    int gain = beep_config_data_get_int("playnet_gain",
            BEEP_PLAYNET_DEFAULT_GAIN);

    player = beep_player_local_init(song_started_cb, gain, NULL);

    stream_ch_listen(&stream_ctx, stream_port, &playnet_stream_events);

    ///// uloop

    uloop_init();

    ctx = beep_ubus_connect("playnet");
    ubus_add_uloop(ctx);

    char obj_name[64] = "beep.playnet";
    if (playnet_flags.name) {
        snprintf(obj_name, 64, "beep.playnet-%s", playnet_flags.name);
    }
    playnet_object.name = obj_name;
    playnet_object_type.name = obj_name;

    int ret = ubus_add_object(ctx, &playnet_object);
    if (ret) {
        fprintf(stderr, "Failed to add object: %s\n", ubus_strerror(ret));
    }

    uloop_run();

    ubus_free(ctx);
    uloop_done();

    ///// End uloop

    beep_player_local_shutdown_wait(player);
}
