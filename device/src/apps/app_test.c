#include <pthread.h>
#include <unistd.h>

#include <libubox/blobmsg_json.h>
#include <libubus.h>

#include "beep/beep_ubus.h"
#include "beep/debug.h"

#include "beep/app.h"

#define OBJ_NAME "beep.app.test"

static struct ubus_context *ctx;
static struct ubus_subscriber test_event;
static struct blob_buf b;

static int audio_token = -1;

enum {
    PLAY_STATION_URL,
    PLAY_STATION_NAME,
    __PLAY_STATION_MAX
};

static const struct blobmsg_policy play_station_policy[] = {
    [PLAY_STATION_URL] = { .name = "url", .type = BLOBMSG_TYPE_STRING },
    [PLAY_STATION_NAME] = { .name = "name", .type = BLOBMSG_TYPE_STRING },
};

void send_ubus_ping(int id, const char* message) {
    // WARNING: This is not thread safe (uses b) but we call it like it is.
    blob_buf_init(&b, 0);
    blobmsg_add_u32(&b, "id", id);
    blobmsg_add_string(&b, "msg", message);
    struct blob_attr* response;
    int ret = safe_ubus_invoke("beep.dummy", "ping", b.head, &response);
    printf("RET: %d\n", ret);

    printf("Response pointer: %p\n", response);
    if (!ret) {
        printf("Response: %s\n", blobmsg_format_json_indent(response, true, 0));
    } else {
        printf("No response\n");
    }
}

static int play_station(
        struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    struct hello_request *hreq;
    struct blob_attr *tb[__PLAY_STATION_MAX];

    send_ubus_ping(14, "hiya! this is ubus thread");

    blobmsg_parse(play_station_policy, __PLAY_STATION_MAX,
            tb, blob_data(msg), blob_len(msg));
    printf("PLAY_STATION called\n");

    if (tb[PLAY_STATION_URL]) {
        printf("Got url %s\n", blobmsg_get_string(tb[PLAY_STATION_URL]));
    }
    if (tb[PLAY_STATION_NAME]) {
        printf("Got name %s\n", blobmsg_get_string(tb[PLAY_STATION_NAME]));
    }

    beep_reply_success(ctx, req, NULL);

    return 0;
}

static const struct ubus_method test_methods[] = {
    UBUS_METHOD("play_station", play_station, play_station_policy),
};

static struct ubus_object_type test_object_type =
    UBUS_OBJECT_TYPE(OBJ_NAME, test_methods);

static struct ubus_object test_object = {
    .name = OBJ_NAME,
    .type = &test_object_type,
    .methods = test_methods,
    .n_methods = ARRAY_SIZE(test_methods),
};

void* test_thread(void *arg) {
    sleep(2);
    printf("test thread: %d\n", is_ubus_thread());
    send_ubus_ping(192, "why hello there! this is test_thread");
    //safe_ubus_send_event(NULL, NULL);
    return NULL;
}

int main(int argc, char **argv)
{
    pthread_t t;
    pthread_create(&t, NULL, test_thread, NULL);
    pthread_detach(t);

    log_beep_main = LOG_CATEGORY_GET("app_test");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    app_start("app_test", &test_object);
}
