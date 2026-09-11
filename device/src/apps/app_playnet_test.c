#include <assert.h>
#include <pthread.h>
#include <sys/time.h>

#include <libubox/blobmsg_json.h>
#include <libubus.h>

#include "beep/app.h"
#include "beep/beep_ubus.h"
#include "beep/flags.h"
#include "beep/beeplib.h"

#define OBJ_NAME_PLAYNET_TEST "beep.app.playnet_test"

static struct blob_buf b;

typedef struct {
    uint32_t chunk_size;
    uint32_t send_size;
} TestParams;

static void *playnet_test_run_test_thread(void *arg) {
    TestParams *params = (TestParams *)arg;
    uint8_t *buf = (uint8_t *)malloc(params->chunk_size);
    double rate;
    uint64_t start_millis;
    uint64_t stop_millis;
    size_t left = params->chunk_size;
    size_t size;
    int token;

    assert(buf);
    while(left--)
        buf[left] = (left & 0x1) ? 0x55 : 0x88;

    LOG_INFO(log_beep_main, "running test with params:");
    LOG_INFO(log_beep_main, "chunk_size: %u send_size: %u",
            params->chunk_size, params->send_size);

    token = audio_acquire(OBJ_NAME_PLAYNET_TEST);
    if (token == -1) {
        LOG_ERROR(log_beep_main, "acquire failed");
        return NULL;
    }

    audio_resume();
    if (!audio_track_begin(token, NULL, NULL, NULL, NULL, NULL, 't', 0)) {
        LOG_ERROR(log_beep_main, "audio_track_begin failed");
        return NULL;
    }

    left = params->send_size;
    start_millis = beep_millis();
    while (left) {
        size = (left < params->chunk_size) ? left : params->chunk_size;
        left -= size;
        if (!audio_buffer(token, buf, size)) {
            LOG_ERROR(log_beep_main, "audio_buffer failed");
            return NULL;
        }
    }
    stop_millis = beep_millis();

    if (!audio_track_end(token)) {
        LOG_ERROR(log_beep_main, "audio_track_end failed");
    }
    audio_acquire(OBJ_NAME_PLAYNET_TEST);

    rate = ((((double)params->send_size) / (stop_millis - start_millis))
            * 1000) / 1024;

    LOG_INFO(log_beep_main, "sent %u bytes in %" PRIu64 " ms (%0.3f kB/s)",
        params->send_size, (stop_millis - start_millis), rate);

    free(buf);
    free(params);

    return NULL;
}

enum {
    RUN_TEST_CHUNK_SIZE,
    RUN_TEST_SEND_SIZE,
    __RUN_TEST_MAX
};

static const struct blobmsg_policy run_test_policy[] = {
    [RUN_TEST_CHUNK_SIZE] = { .name = "chunk_size", .type = BLOBMSG_TYPE_INT32 },
    [RUN_TEST_SEND_SIZE] = { .name = "send_size", .type = BLOBMSG_TYPE_INT32 },
};

static int playnet_test_run_test(
        struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    struct blob_attr *tb[__RUN_TEST_MAX];
    TestParams *params;
    pthread_t t;

    LOG_DEBUG(log_beep_main, "RUN_TEST called");

    blobmsg_parse(run_test_policy, __RUN_TEST_MAX, tb, blob_data(msg),
            blob_len(msg));
    if (!tb[RUN_TEST_CHUNK_SIZE] || !tb[RUN_TEST_SEND_SIZE]) {
        beep_reply_error(ctx, req, "Missing required arg", 0);
        return 0;
    }

    params = (TestParams *)malloc(sizeof(TestParams));
    assert(params);
    memset(params, 0, sizeof(TestParams));

    params->chunk_size = blobmsg_get_u32(tb[RUN_TEST_CHUNK_SIZE]);
    params->send_size = blobmsg_get_u32(tb[RUN_TEST_SEND_SIZE]);

    pthread_create(&t, NULL, playnet_test_run_test_thread, params);
    pthread_detach(t);

    beep_reply_success(ctx, req, NULL);
    return 0;
}

static int playnet_test_get_state(
        struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    blob_buf_init(&b, 0);
    blobmsg_add_u8(&b, "alive", true);
    beep_reply_success(ctx, req, b.head);

    return 0;
}

static int playnet_test_shutdown(
        struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    beep_reply_success(ctx, req, NULL);
    app_end(obj);
    return 0;
}

static const struct ubus_method playnet_test_methods[] = {
    UBUS_METHOD("run_test", playnet_test_run_test, run_test_policy),
    UBUS_METHOD_NOARG("get_state", playnet_test_get_state),
    UBUS_METHOD_NOARG("shutdown", playnet_test_shutdown),
};

static struct ubus_object_type playnet_test_object_type =
    UBUS_OBJECT_TYPE(OBJ_NAME_PLAYNET_TEST, playnet_test_methods);

static struct ubus_object playnet_test_object = {
    .name = OBJ_NAME_PLAYNET_TEST,
    .type = &playnet_test_object_type,
    .methods = playnet_test_methods,
    .n_methods = ARRAY_SIZE(playnet_test_methods),
};

int main(int argc, char **argv)
{
    log_beep_main = LOG_CATEGORY_GET("app_playnet_test");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    beep_flags_init(argc, argv);
    app_init("app_playnet_test", &playnet_test_object);
    app_start();
}


