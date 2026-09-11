#include <assert.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <libubus.h>
#include <libubox/list.h>

#include "app.h"
#include "beep_ubus.h"
#include "audio/streambuf.h"

static struct ubus_context *ctx;

struct audio_event_handler_container {
    struct beep_event_callbacks *callbacks;

    char uuid[40];
    char group_name[256];
    int master_volume;
    bool playing;
    bool stopped;
    uint32_t streambuf_used;
    uint32_t output_used;
    uint32_t written_track_time;
    uint32_t duration;

    bool _set;
} ev_handler;

typedef struct {
    struct ubus_object* obj;
    struct uloop_timeout t;
} AppShutdown;

static pthread_mutex_t beep_app_mutex;

static void beep_app_lock(void) {
    assert(pthread_mutex_lock(&beep_app_mutex) == 0);
}

static void beep_app_unlock(void) {
    assert(pthread_mutex_unlock(&beep_app_mutex) == 0);
}

void on_app_shutdown(struct uloop_timeout* t) {
    AppShutdown* app_shutdown = (AppShutdown*)((uint8_t*)t -
            offsetof(AppShutdown, t));

    if (app_shutdown->obj)
        ubus_remove_object_async(ctx, app_shutdown->obj, NULL, NULL);

    // There is no ubus_remove_uloop but it would do this.
    uloop_fd_delete(&ctx->sock);

    beep_ubus_disconnect(ctx);
    ctx = NULL;

    uloop_end();

    free(app_shutdown);
}

void app_end(struct ubus_object* obj) {
    if (!ctx)
        return;

    AppShutdown* app_shutdown = (AppShutdown*)malloc(sizeof(AppShutdown));
    // If failure on shutdown just abort.
    if (!app_shutdown)
        abort();
    memset(app_shutdown, 0, sizeof(AppShutdown));

    app_shutdown->obj = obj;
    app_shutdown->t.cb = on_app_shutdown;
    // If this is called from a ubus method and the object is removed it will
    // cause the method to timeout for the caller.  Setup an immediate
    // timeout that will do the shutdown on the next uloop iteration.
    uloop_timeout_set(&app_shutdown->t, 0);
}

struct ubus_context* app_init(const char* app_name, struct ubus_object* obj) {
    int ret;

    pthread_mutex_init(&beep_app_mutex, NULL);

    uloop_init();

    ctx = beep_ubus_connect(app_name);
    if (!ctx) {
        LOG_ERROR(log_beep_main, "Failed to connect to ubus");
        exit(1);
    }

    ubus_add_uloop(ctx);

    if (obj) {
        ret = ubus_add_object(ctx, obj);
        if (ret) {
            LOG_ERROR(log_beep_main,
                    "Failed to add object: %s", ubus_strerror(ret));
            exit(1);
        }
    }

    return ctx;
}

void app_start(void) {
    LOG_INFO(log_beep_main, "App started.");
    uloop_run();

    uloop_done();
}

enum {
    DISTRIBUTOR_RESPONSE_ACQUIRE_TOKEN,
    __DISTRIBUTOR_RESPONSE_ACQUIRE_MAX
};

static const struct blobmsg_policy distributor_response_acquire_policy[] = {
    [DISTRIBUTOR_RESPONSE_ACQUIRE_TOKEN] = {
        .name = "token", .type = BLOBMSG_TYPE_INT32 }
};

static bool acquiring = false;

int audio_acquire(const char* app_ubus_obj) {
    struct blob_attr* response = NULL;
    int token = -1;

    struct blob_buf* args = calloc(1, sizeof(struct blob_buf));
    blob_buf_init(args, 0);
    blobmsg_add_string(args, "app_ubus_obj", app_ubus_obj);

    // beep_ubus_invoke allows other ubus methods to run while it blocks.
    // we need to guard against two acquires in flight at once in order to
    // guarantee that when audio_acquire returns the returned token is valid.
    if (acquiring) {
        LOG_INFO(log_beep_main, "acquire already in flight");
        goto out;
    }

    acquiring = true;

    beep_app_lock();
    ev_handler.streambuf_used = 0;
    ev_handler.output_used = 0;
    ev_handler.written_track_time = 0;
    ev_handler.duration = 0;
    beep_app_unlock();

    int ret = beep_ubus_invoke(
            "beep.distributor", "acquire", args->head, &response);
    acquiring = false;

    if (ret) {
        LOG_ERROR(log_beep_main, "ubus error: %s", ubus_strerror(ret));
        goto out;
    }

    struct blob_attr *parsed[__BEEP_RESPONSE_MAX];
    if (!beep_parse_response(response, parsed)) {
        goto out;
    }
    if (!blobmsg_get_bool(parsed[BEEP_RESPONSE_SUCCESS])) {
        LOG_ERROR(log_beep_main, "Got beep error.  code: %d  msg: %s",
                blobmsg_get_u32(parsed[BEEP_RESPONSE_ERROR_CODE]),
                blobmsg_get_string(parsed[BEEP_RESPONSE_ERROR_MESSAGE]));
        goto out;
    }

    // Parsed out token
    struct blob_attr *result[__BEEP_RESPONSE_MAX];
    blobmsg_parse(
            distributor_response_acquire_policy,
            __DISTRIBUTOR_RESPONSE_ACQUIRE_MAX,
            result,
            blobmsg_data(parsed[BEEP_RESPONSE_RESULT]),
            blobmsg_data_len(parsed[BEEP_RESPONSE_RESULT]));
    if (!result[DISTRIBUTOR_RESPONSE_ACQUIRE_TOKEN]) {
        LOG_ERROR(log_beep_main, "response missing token");
        goto out;
    }

    token = blobmsg_get_u32(result[DISTRIBUTOR_RESPONSE_ACQUIRE_TOKEN]);
    LOG_INFO(log_beep_main, "acquire succeeded with token: %d", token);

out:
    blob_buf_free(args);
    free(args);
    free(response);

    return token;
}

int audio_can_track_begin(int token) {
    int result = -1;
    struct blob_buf* args = calloc(1, sizeof(struct blob_buf));
    blob_buf_init(args, 0);
    blobmsg_add_u32(args, "token", token);

    struct blob_attr* response;
    int ret = beep_ubus_invoke(
            "beep.distributor", "can_track_begin", args->head, &response);
    if (ret) {
        LOG_ERROR(log_beep_main, "ubus error: %s", ubus_strerror(ret));
        goto out;
    }

    struct blob_attr *parsed[__BEEP_RESPONSE_MAX];
    if (!beep_parse_response(response, parsed)) {
        goto out;
    }
    if (!blobmsg_get_bool(parsed[BEEP_RESPONSE_SUCCESS])) {
        int beep_error_code =
                blobmsg_get_u32(parsed[BEEP_RESPONSE_ERROR_CODE]);
        if (beep_error_code == BEEP_UBUS_ERROR_DISTRIBUTOR_COMMAND_WOULD_BLOCK) {
            result = 0;
        }
        goto out;
    }

    result = 1;

out:
    blob_buf_free(args);
    free(args);
    free(response);

    return result;
}

// TODO: Clarify which parts of this API are optional and enforce.
// audio_type:
//     'm' for mp3
//     'a' for aac
bool audio_track_begin(
        int token, const char* track_id,
        const char* title0, const char* title1, const char* title2,
        const char* image_url, const char audio_type, int content_length) {
    int success = false;
    struct blob_buf* args = calloc(1, sizeof(struct blob_buf));
    blob_buf_init(args, 0);
    blobmsg_add_u32(args, "token", token);
    blobmsg_add_u32(args, "content_length", content_length);
    if (title1) {
        void* r = blobmsg_open_table(args, "track_info");
        if (track_id) {
            blobmsg_add_string(args, "track_id", track_id);
        }
        if (title0) {
            blobmsg_add_string(args, "title0", title0);
        }
        if (title1) {
            blobmsg_add_string(args, "title1", title1);
        }
        if (title2) {
            blobmsg_add_string(args, "title2", title2);
        }
        if (image_url) {
            blobmsg_add_string(args, "image_url", image_url);
        }
        blobmsg_close_table(args, r);
    }
    char _audio_type[] = {audio_type, '\0'};
    blobmsg_add_string(args, "audio_type", _audio_type);


    struct blob_attr* response;
    int ret = beep_ubus_invoke(
            "beep.distributor", "track_begin", args->head, &response);
    if (ret) {
        LOG_ERROR(log_beep_main, "ubus error: %s", ubus_strerror(ret));
        goto out;
    }

    struct blob_attr *parsed[__BEEP_RESPONSE_MAX];
    if (!beep_parse_response(response, parsed)) {
        goto out;
    }
    if (!blobmsg_get_bool(parsed[BEEP_RESPONSE_SUCCESS])) {
        LOG_ERROR(log_beep_main, "Got beep error.  code: %d  msg: %s",
                blobmsg_get_u32(parsed[BEEP_RESPONSE_ERROR_CODE]),
                blobmsg_get_string(parsed[BEEP_RESPONSE_ERROR_MESSAGE]));
        goto out;
    }

    success = true;

out:
    blob_buf_free(args);
    free(args);
    free(response);

    return success;
}

// TODO: We can get rid of a lot of duplicated code in these audio_*
//     functions.
bool audio_track_end(int token) {
    int success = false;
    struct blob_buf* args = calloc(1, sizeof(struct blob_buf));
    blob_buf_init(args, 0);
    blobmsg_add_u32(args, "token", token);

    struct blob_attr* response;
    int ret = beep_ubus_invoke(
            "beep.distributor", "track_end", args->head, &response);
    if (ret) {
        LOG_ERROR(log_beep_main, "ubus error: %s", ubus_strerror(ret));
        goto out;
    }

    struct blob_attr *parsed[__BEEP_RESPONSE_MAX];
    if (!beep_parse_response(response, parsed)) {
        goto out;
    }
    if (!blobmsg_get_bool(parsed[BEEP_RESPONSE_SUCCESS])) {
        int error_code = blobmsg_get_u32(parsed[BEEP_RESPONSE_ERROR_CODE]);
        // This would happen if something else acquired and took our token,
        // meaning track_end gets called implicitly by whatever just acquired
        // audio.
        if (error_code != BEEP_UBUS_ERROR_DISTRIBUTOR_INVALID_TOKEN) {
            LOG_ERROR(log_beep_main, "Got beep error.  code: %d  msg: %s",
                    blobmsg_get_u32(parsed[BEEP_RESPONSE_ERROR_CODE]),
                    blobmsg_get_string(parsed[BEEP_RESPONSE_ERROR_MESSAGE]));
        }
        goto out;
    }

    success = true;

out:
    blob_buf_free(args);
    free(args);
    free(response);

    return success;
}

bool set_track_info(
        int token,
        const char* title0, const char* title1, const char* title2,
        const char* image_url) {
    int success = false;
    struct blob_buf* args = calloc(1, sizeof(struct blob_buf));
    blob_buf_init(args, 0);
    blobmsg_add_u32(args, "token", token);
    void* r = blobmsg_open_table(args, "track_info");
    if (title0) {
        blobmsg_add_string(args, "title0", title0);
    }
    if (title1) {
        blobmsg_add_string(args, "title1", title1);
    }
    if (title2) {
        blobmsg_add_string(args, "title2", title2);
    }
    if (image_url) {
        blobmsg_add_string(args, "image_url", image_url);
    }
    blobmsg_close_table(args, r);

    struct blob_attr* response;
    int ret = beep_ubus_invoke(
            "beep.distributor", "set_track_info", args->head, &response);
    if (ret) {
        LOG_ERROR(log_beep_main, "ubus error: %s", ubus_strerror(ret));
        goto out;
    }

    struct blob_attr *parsed[__BEEP_RESPONSE_MAX];
    if (!beep_parse_response(response, parsed)) {
        goto out;
    }
    if (!blobmsg_get_bool(parsed[BEEP_RESPONSE_SUCCESS])) {
        LOG_ERROR(log_beep_main, "Got beep error.  code: %d  msg: %s",
                blobmsg_get_u32(parsed[BEEP_RESPONSE_ERROR_CODE]),
                blobmsg_get_string(parsed[BEEP_RESPONSE_ERROR_MESSAGE]));
        goto out;
    }

    success = true;

out:
    blob_buf_free(args);
    free(args);
    free(response);

    return success;
}

bool audio_flush(int token) {
    int success = false;
    struct blob_buf* args = calloc(1, sizeof(struct blob_buf));
    blob_buf_init(args, 0);
    blobmsg_add_u32(args, "token", token);

    struct blob_attr* response;
    int ret = beep_ubus_invoke(
            "beep.distributor", "flush", args->head, &response);
    if (ret) {
        LOG_ERROR(log_beep_main, "ubus error: %s", ubus_strerror(ret));
        goto out;
    }

    struct blob_attr *parsed[__BEEP_RESPONSE_MAX];
    if (!beep_parse_response(response, parsed)) {
        goto out;
    }
    if (!blobmsg_get_bool(parsed[BEEP_RESPONSE_SUCCESS])) {
        LOG_ERROR(log_beep_main, "Got beep error.  code: %d  msg: %s",
                blobmsg_get_u32(parsed[BEEP_RESPONSE_ERROR_CODE]),
                blobmsg_get_string(parsed[BEEP_RESPONSE_ERROR_MESSAGE]));
        goto out;
    }

    success = true;

out:
    blob_buf_free(args);
    free(args);
    free(response);

    return success;
}

int audio_can_buffer(int token, size_t size) {
    int result = -1;
    struct blob_buf* args = calloc(1, sizeof(struct blob_buf));
    blob_buf_init(args, 0);
    blobmsg_add_u32(args, "token", token);
    blobmsg_add_u32(args, "data_len", size);

    struct blob_attr* response;
    int ret = beep_ubus_invoke(
            "beep.distributor", "can_buffer", args->head, &response);
    if (ret) {
        LOG_ERROR(log_beep_main, "ubus error: %s", ubus_strerror(ret));
        goto out;
    }

    struct blob_attr *parsed[__BEEP_RESPONSE_MAX];
    if (!beep_parse_response(response, parsed)) {
        goto out;
    }
    if (!blobmsg_get_bool(parsed[BEEP_RESPONSE_SUCCESS])) {
        int beep_error_code =
                blobmsg_get_u32(parsed[BEEP_RESPONSE_ERROR_CODE]);
        if (beep_error_code == BEEP_UBUS_ERROR_DISTRIBUTOR_COMMAND_WOULD_BLOCK) {
            result = 0;
        }
        goto out;
    }

    result = 1;

out:
    blob_buf_free(args);
    free(args);
    free(response);

    return result;
}

bool audio_buffer(int token, uint8_t* ptr, size_t size) {
    int success = false;

    struct blob_buf* args = calloc(1, sizeof(struct blob_buf));
    blob_buf_init(args, 0);
    blobmsg_add_u32(args, "token", token);

    // Need to null terminate because ubus checks that there is a \0 on the
    // end, and does nothing if it doesn't find it.
    char* data_terminated = malloc(size + 1);
    memcpy(data_terminated, ptr, size);
    data_terminated[size] = '\0';
    if (blobmsg_add_field(args, BLOBMSG_TYPE_STRING, "data",
              data_terminated, size + 1) == -1) {
        LOG_ERROR(log_beep_main, "Error adding data field");
        abort();
    }
    free(data_terminated);

    struct blob_attr* response;
    int ret = beep_ubus_invoke(
            "beep.distributor", "buffer", args->head, &response);
    if (ret) {
        LOG_ERROR(log_beep_main, "ubus error: %s", ubus_strerror(ret));
        // TODO: Should die here, system is in an invalid state.
        goto out;
    }

    struct blob_attr *parsed[__BEEP_RESPONSE_MAX];
    if (!beep_parse_response(response, parsed)) {
        goto out;
    }
    if (!blobmsg_get_bool(parsed[BEEP_RESPONSE_SUCCESS])) {
        LOG_ERROR(log_beep_main, "Got beep error.  code: %d  msg: %s",
                blobmsg_get_u32(parsed[BEEP_RESPONSE_ERROR_CODE]),
                blobmsg_get_string(parsed[BEEP_RESPONSE_ERROR_MESSAGE]));
        goto out;
    }

    success = true;

out:
    blob_buf_free(args);
    free(args);
    free(response);

    return success;
}

bool set_station(
        int token, const char* station_id, const char* station_name,
        const char* station_image_url,
        const char* play_station_method, struct blob_attr* play_station_args) {
    int success = false;
    struct blob_attr *cur;
    struct blob_buf *args;
    int rem;

    if (!station_name) {
        LOG_ERROR(log_beep_main, "Illegal argument: station_name may not be NULL");
        abort();
    }

    args = calloc(1, sizeof(struct blob_buf));

    blob_buf_init(args, 0);
    blobmsg_add_u32(args, "token", token);
    blobmsg_add_string(args, "station_id", station_id);
    blobmsg_add_string(args, "station_name", station_name);
    if (station_image_url) {
        blobmsg_add_string(args, "station_image_url", station_image_url);
    }

    blobmsg_add_string(args, "play_station_method", play_station_method);

    void* r = blobmsg_open_table(args, "play_station_args");
    if (play_station_args) {
        blob_for_each_attr(cur, play_station_args, rem) {
            blobmsg_add_blob(args, cur);
        }
    }
    blobmsg_close_table(args, r);

    struct blob_attr* response;
    int ret = beep_ubus_invoke(
            "beep.distributor", "set_station", args->head, &response);
    if (ret) {
        LOG_ERROR(log_beep_main, "ubus error: %s", ubus_strerror(ret));
        goto out;
    }

    struct blob_attr *parsed[__BEEP_RESPONSE_MAX];
    if (!beep_parse_response(response, parsed)) {
        goto out;
    }
    if (!blobmsg_get_bool(parsed[BEEP_RESPONSE_SUCCESS])) {
        LOG_ERROR(log_beep_main, "Got beep error.  code: %d  msg: %s",
                blobmsg_get_u32(parsed[BEEP_RESPONSE_ERROR_CODE]),
                blobmsg_get_string(parsed[BEEP_RESPONSE_ERROR_MESSAGE]));
        goto out;
    }

    success = true;

out:
    blob_buf_free(args);
    free(args);
    free(response);

    return success;
}

static bool audio_play(bool play) {
    int success = false;
    struct blob_buf* args = calloc(1, sizeof(struct blob_buf));
    blob_buf_init(args, 0);

    struct blob_attr* response;
    int ret = beep_ubus_invoke(
            "beep.distributor", play ? "resume" : "do_pause", args->head, &response);
    if (ret) {
        LOG_ERROR(log_beep_main, "ubus error: %s", ubus_strerror(ret));
        goto out;
    }

    struct blob_attr *parsed[__BEEP_RESPONSE_MAX];
    if (!beep_parse_response(response, parsed)) {
        goto out;
    }
    if (!blobmsg_get_bool(parsed[BEEP_RESPONSE_SUCCESS])) {
        LOG_ERROR(log_beep_main, "Got beep error.  code: %d  msg: %s",
                blobmsg_get_u32(parsed[BEEP_RESPONSE_ERROR_CODE]),
                blobmsg_get_string(parsed[BEEP_RESPONSE_ERROR_MESSAGE]));
        goto out;
    }

    success = true;

out:
    blob_buf_free(args);
    free(args);
    free(response);

    return success;
}

bool audio_resume() {
    return audio_play(true);
}

bool audio_pause() {
    return audio_play(false);
}

bool msg_socket_send_message(const char* app_id, const char* sender_id,
        const char* msg_namespace, const char* message) {
    struct blob_buf* args = calloc(1, sizeof(struct blob_buf));
    struct blob_attr* response;
    struct blob_attr* parsed[__BEEP_RESPONSE_MAX];
    int ret;
    bool success = false;

    blob_buf_init(args, 0);
    blobmsg_add_string(args, "app_id", app_id);
    blobmsg_add_string(args, "sender_id", sender_id);
    blobmsg_add_string(args, "namespace", msg_namespace);
    blobmsg_add_string(args, "message", message);

    ret = beep_ubus_invoke("beep.comm", "msg_socket_send_message",
            args->head, &response);

    if (ret) {
        LOG_ERROR(log_beep_main, "ubus error: %s", ubus_strerror(ret));
        goto out;
    }

    if (!beep_parse_response(response, parsed)) {
        goto out;
    }

    if (!blobmsg_get_bool(parsed[BEEP_RESPONSE_SUCCESS])) {
        LOG_ERROR(log_beep_main, "Got beep error.  code: %d  msg: %s",
                blobmsg_get_u32(parsed[BEEP_RESPONSE_ERROR_CODE]),
                blobmsg_get_string(parsed[BEEP_RESPONSE_ERROR_MESSAGE]));
        goto out;
    }

    success = true;

out:
    blob_buf_free(args);
    free(args);
    free(response);

    return success;
}

bool msg_socket_close(const char* app_id, const char* sender_id) {
    struct blob_buf* args = calloc(1, sizeof(struct blob_buf));
    struct blob_attr* response;
    struct blob_attr* parsed[__BEEP_RESPONSE_MAX];
    int ret;
    bool success = false;

    blob_buf_init(args, 0);
    blobmsg_add_string(args, "app_id", app_id);
    blobmsg_add_string(args, "sender_id", sender_id);

    ret = beep_ubus_invoke("beep.comm", "msg_socket_close",
            args->head, &response);

    if (ret) {
        LOG_ERROR(log_beep_main, "ubus error: %s", ubus_strerror(ret));
        goto out;
    }

    if (!beep_parse_response(response, parsed)) {
        goto out;
    }

    if (!blobmsg_get_bool(parsed[BEEP_RESPONSE_SUCCESS])) {
        LOG_ERROR(log_beep_main, "Got beep error.  code: %d  msg: %s",
                blobmsg_get_u32(parsed[BEEP_RESPONSE_ERROR_CODE]),
                blobmsg_get_string(parsed[BEEP_RESPONSE_ERROR_MESSAGE]));
        goto out;
    }

    success = true;

out:
    blob_buf_free(args);
    free(args);
    free(response);

    return success;
}

static void assert_event_handler(const char *func_name) {
    if (!ev_handler._set) {
        LOG_ERROR(log_beep_main, "Attempted to call %s but \
                set_audio_event_handler was not called", func_name);
        abort();
    }
}

int get_master_volume(void) {
    assert_event_handler("get_master_volume");

    beep_app_lock();
    uint32_t val = ev_handler.master_volume;
    beep_app_unlock();

    return val;
}

uint32_t get_streambuf_used(void) {
    assert_event_handler("get_streambuf_used");

    beep_app_lock();
    uint32_t val = ev_handler.streambuf_used;
    beep_app_unlock();

    return val;
}

uint32_t get_output_used(void) {
    assert_event_handler("get_output_used");

    beep_app_lock();
    uint32_t val = ev_handler.output_used;
    beep_app_unlock();

    return val;
}

uint32_t get_written_track_time(void) {
    assert_event_handler("written_track_time");

    beep_app_lock();
    uint32_t val = ev_handler.written_track_time;
    beep_app_unlock();

    return val;
}

uint32_t get_track_duration(void) {
    assert_event_handler("duration");

    beep_app_lock();
    uint32_t val = ev_handler.duration;
    beep_app_unlock();

    return val;
}

// TODO: This only works for PCM
uint32_t get_buffered_bytes(void) {
    assert_event_handler("get_buffered_bytes");

    beep_app_lock();
    uint32_t val = ev_handler.streambuf_used + (ev_handler.output_used / 2);
    beep_app_unlock();

    return val;
}

/*
 * This code doesn't depend on the event reception stuff, but makes sense
 * to put it next to get_master_volume()
 */
void set_master_volume(int volume) {
    struct blob_attr *response;
    struct blob_buf *args = calloc(1, sizeof(struct blob_buf));
    blob_buf_init(args, 0);
    blobmsg_add_u32(args, "volume", volume);

    int ret = beep_ubus_invoke(
            "beep.distributor",
            "set_master_volume",
            args->head, &response);

    if (ret) {
        LOG_ERROR(log_beep_main, "ubus error: %s", ubus_strerror(ret));
        goto out;
    }

    struct blob_attr *parsed[__BEEP_RESPONSE_MAX];
    if (!beep_parse_response(response, parsed)) {
        goto out;
    }

    if (!blobmsg_get_bool(parsed[BEEP_RESPONSE_SUCCESS])) {
        LOG_ERROR(log_beep_main, "Got beep error.  code: %d  msg: %s",
                blobmsg_get_u32(parsed[BEEP_RESPONSE_ERROR_CODE]),
                blobmsg_get_string(parsed[BEEP_RESPONSE_ERROR_MESSAGE]));
        goto out;
    }

out:
    blob_buf_free(args);
    free(args);
    free(response);
}

void set_track_volume_scalar(int volume) {
    struct blob_attr *response;
    struct blob_buf *args = calloc(1, sizeof(struct blob_buf));
    blob_buf_init(args, 0);
    blobmsg_add_u32(args, "volume", volume);

    int ret = beep_ubus_invoke(
            "beep.distributor",
            "set_track_volume_scalar",
            args->head, &response);

    if (ret) {
        LOG_ERROR(log_beep_main, "ubus error: %s", ubus_strerror(ret));
        goto out;
    }

    struct blob_attr *parsed[__BEEP_RESPONSE_MAX];
    if (!beep_parse_response(response, parsed)) {
        goto out;
    }

    if (!blobmsg_get_bool(parsed[BEEP_RESPONSE_SUCCESS])) {
        LOG_ERROR(log_beep_main, "Got beep error.  code: %d  msg: %s",
                blobmsg_get_u32(parsed[BEEP_RESPONSE_ERROR_CODE]),
                blobmsg_get_string(parsed[BEEP_RESPONSE_ERROR_MESSAGE]));
        goto out;
    }

out:
    blob_buf_free(args);
    free(args);
    free(response);
}

// TODO: This is still not threadsafe. We pass pointers into our ev_handler
// struct to the callbacks without a lock held. We don't want to hold the
// lock while calling callbacks, so we should pass copies of the values
// instead.
static void ev_handler_cb(const char *component, struct blob_attr *state,
        struct blob_attr *event_data, const char *event_type) {
    if (!strcmp(component, "distributor")) {
        struct blob_attr *tb_state[__DISTRIBUTOR_STATE_MAX];

        if (blobmsg_parse(distributor_state_policy, __DISTRIBUTOR_STATE_MAX,
                    tb_state, blobmsg_data(state), blobmsg_data_len(state))) {
            LOG_WARN(log_beep_main, "Failed to parse distributor state");
            return;
        }

        int new_volume =
                tb_state[DISTRIBUTOR_STATE_MASTER_VOLUME] ?
                blobmsg_get_u32(tb_state[DISTRIBUTOR_STATE_MASTER_VOLUME]) :
                -1;

        if (new_volume < 0 || ev_handler.master_volume != new_volume) {
            beep_app_lock();
            ev_handler.master_volume = new_volume;
            beep_app_unlock();
            if (ev_handler.callbacks->on_volume_event != NULL) {
                (*ev_handler.callbacks->on_volume_event)
                        (ev_handler.master_volume, ev_handler.callbacks->userdata);
            }
        }

        beep_app_lock();
        ev_handler.streambuf_used =
                tb_state[DISTRIBUTOR_STATE_STREAMBUF_USED] ?
                blobmsg_get_u32(tb_state[DISTRIBUTOR_STATE_STREAMBUF_USED]) :
                0;

        ev_handler.output_used =
                tb_state[DISTRIBUTOR_STATE_OUTPUT_USED] ?
                blobmsg_get_u32(tb_state[DISTRIBUTOR_STATE_OUTPUT_USED]) : 0;

        ev_handler.written_track_time =
                tb_state[DISTRIBUTOR_STATE_WRITTEN_TRACK_TIME] ?
                blobmsg_get_u32(tb_state[DISTRIBUTOR_STATE_WRITTEN_TRACK_TIME]) : 0;

        ev_handler.duration =
                tb_state[DISTRIBUTOR_STATE_DURATION] ?
                blobmsg_get_u32(tb_state[DISTRIBUTOR_STATE_DURATION]) : 0;
        beep_app_unlock();

        if (ev_handler.callbacks->on_buffer_event != NULL) {
            (*ev_handler.callbacks->on_buffer_event)(get_buffered_bytes(),
                    ev_handler.callbacks->userdata);
        }

        // playing is true if audio state is not paused, i.e. "playing" or "working"
        bool playing =
                tb_state[DISTRIBUTOR_STATE_AUDIO_STATE] ?
                strcmp(blobmsg_get_string(tb_state[DISTRIBUTOR_STATE_AUDIO_STATE]),
                            "paused") != 0 : false;

        if (playing != ev_handler.playing) {
            beep_app_lock();
            ev_handler.playing = playing;
            beep_app_unlock();
            if (ev_handler.callbacks->on_playpause_event != NULL) {
                (*ev_handler.callbacks->on_playpause_event)
                        (ev_handler.playing, ev_handler.callbacks->userdata);
            }
        }

        bool stopped =
                tb_state[DISTRIBUTOR_STATE_PLAY_STATE] ?
                strcmp(blobmsg_get_string(tb_state[DISTRIBUTOR_STATE_PLAY_STATE]),
                            "stopped") == 0 : false;
        if (stopped != ev_handler.stopped) {
            beep_app_lock();
            ev_handler.stopped = stopped;
            beep_app_unlock();
            if (stopped) {
                if (ev_handler.callbacks->on_stopped_event != NULL) {
                    (*ev_handler.callbacks->on_stopped_event)
                            (ev_handler.callbacks->userdata);
                }

            }
        }

    } else if (!strcmp(component,"manager")) {
        struct blob_attr *tb_state[__MANAGER_STATE_MAX];
        struct blob_attr *tb_local[__MANAGER_LOCAL_MAX];
        struct blob_attr *tb_device[__MANAGER_DEVICE_MAX];

        if (blobmsg_parse(manager_state_policy, __MANAGER_STATE_MAX,
                    tb_state, blobmsg_data(state), blobmsg_data_len(state))) {
            LOG_WARN(log_beep_main, "Failed to parse manager state");
            return;
        }

        if (!tb_state[MANAGER_STATE_LOCAL_DEVICE] || !tb_state[MANAGER_STATE_DEVICES]) {
            LOG_DEBUG(log_beep_main, "Ignoring incomplete manager state");
            return;
        }

        if (blobmsg_parse(manager_local_policy, __MANAGER_LOCAL_MAX,
                    tb_local, blobmsg_data(tb_state[MANAGER_STATE_LOCAL_DEVICE]),
                    blobmsg_data_len(tb_state[MANAGER_STATE_LOCAL_DEVICE]))) {
            LOG_WARN(log_beep_main, "Failed to parse manager local device table");
            return;
        }

        const char *local_source_id =
                blobmsg_data(tb_local[MANAGER_LOCAL_SOURCE_ID]);
        LOG_INFO(log_beep_main, "local_source_id = %s", local_source_id);
        if (strcmp(local_source_id,"-1") == 0) {
            LOG_INFO(log_beep_main, "(not a master) ignoring...");
            return;
        }

        struct blob_attr *attr;
        struct blob_attr *devices_table = blobmsg_data(tb_state[MANAGER_STATE_DEVICES]);
        int len = blobmsg_data_len(tb_state[MANAGER_STATE_DEVICES]);

        char name_buffer[32];

        beep_app_lock();

        ev_handler.group_name[0] = 0;

        __blob_for_each_attr(attr, devices_table, len) {
            if (blobmsg_parse(manager_device_policy, __MANAGER_DEVICE_MAX,
                        tb_device, blobmsg_data(attr), blobmsg_data_len(attr))) {
                LOG_WARN(log_beep_main, "Failed to parse device table");
                beep_app_unlock();
                return;
            }
            // This is private user info, don't log in production
            // LOG_INFO(log_beep_main, "Found device: %s (%s)",
            //         (char *)blobmsg_data(tb_device[MANAGER_DEVICE_NAME]),
            //         (char *)blobmsg_data(tb_device[MANAGER_DEVICE_SINK_ID]));
            if (strcmp(blobmsg_data(tb_device[MANAGER_DEVICE_SINK_ID]),
                        local_source_id) == 0) { // Match
                if (!strlen(ev_handler.group_name)) {
                    strncpy(name_buffer,
                            (char *)blobmsg_data(tb_device[MANAGER_DEVICE_NAME]), 32);
                } else {
                    snprintf(name_buffer, 32, ", %s",
                            (char *)blobmsg_data(tb_device[MANAGER_DEVICE_NAME]));
                }
                strncat(ev_handler.group_name, name_buffer, 256);
            }
        }

        strncpy(ev_handler.uuid, blobmsg_data(tb_local[MANAGER_LOCAL_SET_UUID]),
                40);
        ev_handler.uuid[39] = 0;

        beep_app_unlock();

        if (ev_handler.callbacks->on_group_event != NULL) {
            (*ev_handler.callbacks->on_group_event)
                    (ev_handler.group_name, ev_handler.uuid,
                     ev_handler.callbacks->userdata);
        }
    }
}

void set_beep_event_handler(struct beep_event_callbacks *callbacks) {
    ev_handler.callbacks = callbacks;
    ev_handler.group_name[0] = 0;
    ev_handler._set = true;

    beep_ubus_subscribe("manager", ev_handler_cb, NULL);
    beep_ubus_subscribe("distributor", ev_handler_cb, NULL);
}
