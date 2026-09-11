#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <libubox/uloop.h>
#include <libubus.h>

#include "beep/beep_ubus.h"
#include "beep/config.h"
#include "beep/debug.h"
#include "beep/flags.h"

struct flag_vals {
    char *obj_name;
    int num_events;
    int event_interval;
};

struct flag_vals uhammer_flags = {
    .obj_name = "uhammer",
    .num_events = 0,
    .event_interval = 1000,
};

static const BeepFlag flags[] = {
    BEEP_FLAG("name", BEEP_FLAG_STRING, &uhammer_flags.obj_name, NULL, NULL),
    BEEP_FLAG("num_events", BEEP_FLAG_INT, &uhammer_flags.num_events, NULL, NULL),
    BEEP_FLAG("event_interval", BEEP_FLAG_INT, &uhammer_flags.event_interval, NULL, NULL)
};

BEEP_INIT_FLAGS(flags);

/*
 * ubus method: echo(message)
 *
 * Returns a beep response with <message> immediately
 */

enum {
    ECHO_MESSAGE,
    __ECHO_MAX
};

const struct blobmsg_policy echo_policy[] = {
    [ECHO_MESSAGE] = { .name = "message", .type = BLOBMSG_TYPE_STRING },
};

static int hammer_echo(
        struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    struct blob_attr *tb[__ECHO_MAX];
    struct blob_buf rb;
    memset(&rb, 0, sizeof(struct blob_buf));

    blobmsg_parse(echo_policy, ARRAY_SIZE(echo_policy), tb,
            blob_data(msg), blob_len(msg));
    char *message = NULL;

    if(!tb[ECHO_MESSAGE]) {
        beep_reply_error(ctx, req, "Missing parameter: message", 0);
        return 0;
    }
    message = blobmsg_data(tb[ECHO_MESSAGE]);

    blob_buf_init(&rb, 0);
    blobmsg_add_string(&rb, "message", message);
    beep_reply_success(ctx, req, rb.head);
    blob_buf_free(&rb);
    return 0;
}

/*
 * ubus method: blocking_echo(message, delay)
 *
 * Returns a beep response with <message> after <delay> ms, but does not
 * defer the request as it should and instead blocks ubus/uloop
 *
 */

enum {
    BLOCKING_ECHO_MESSAGE,
    BLOCKING_ECHO_DELAY,
    __BLOCKING_ECHO_MAX
};

const struct blobmsg_policy blocking_echo_policy[] = {
    [BLOCKING_ECHO_MESSAGE] = { .name = "message", .type = BLOBMSG_TYPE_STRING },
    [BLOCKING_ECHO_DELAY] = { .name = "delay", .type = BLOBMSG_TYPE_INT32 },
};

static int hammer_blocking_echo(
        struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    struct blob_attr *tb[__BLOCKING_ECHO_MAX];
    struct blob_buf rb;
    memset(&rb, 0, sizeof(struct blob_buf));

    blobmsg_parse(blocking_echo_policy, ARRAY_SIZE(blocking_echo_policy), tb,
            blob_data(msg), blob_len(msg));
    char *message = NULL;
    uint32_t delay;

    if(!tb[ECHO_MESSAGE]) {
        beep_reply_error(ctx, req, "Missing parameter: message", 0);
        return 0;
    }
    message = blobmsg_data(tb[BLOCKING_ECHO_MESSAGE]);

    if(!tb[BLOCKING_ECHO_DELAY]) {
        beep_reply_error(ctx, req, "Missing parameter: delay", 0);
        return 0;
    }
    delay = blobmsg_get_u32(tb[BLOCKING_ECHO_DELAY]);

    LOG_INFO(log_beep_main, "Begin blocking for %dms.", delay);
    usleep(delay * 1000);
    LOG_INFO(log_beep_main, "Done blocking.");

    blob_buf_init(&rb, 0);
    blobmsg_add_string(&rb, "message", message);
    beep_reply_success(ctx, req, rb.head);
    blob_buf_free(&rb);
    return 0;
}

/*
 * ubus method: delayed_echo(message, delay)
 *
 * Returns a beep response with <message> after <delay> ms
 */

enum {
    DELAYED_ECHO_MESSAGE,
    DELAYED_ECHO_DELAY,
    __DELAYED_ECHO_MAX
};

const struct blobmsg_policy delayed_echo_policy[] = {
    [DELAYED_ECHO_MESSAGE] = { .name = "message", .type = BLOBMSG_TYPE_STRING },
    [DELAYED_ECHO_DELAY] = { .name = "delay", .type = BLOBMSG_TYPE_INT32 },
};

struct delayed_echo_req {
    struct ubus_request_data req;
    struct ubus_context *ctx;
    struct uloop_timeout timeout;

    char *message;
};

static void delayed_echo_timeout(struct uloop_timeout *t) {
    struct delayed_echo_req *held_req =
            container_of(t, struct delayed_echo_req, timeout);
    struct blob_buf rb;
    memset(&rb, 0, sizeof(struct blob_buf));

    blob_buf_init(&rb, 0);
    blobmsg_add_string(&rb, "message", held_req->message);
    beep_reply_success(held_req->ctx, &held_req->req, rb.head);
    ubus_complete_deferred_request(held_req->ctx, &held_req->req, 0);

    blob_buf_free(&rb);
    free(held_req->message);
    free(held_req);
}

static int hammer_delayed_echo(
        struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    struct blob_attr *tb[__DELAYED_ECHO_MAX];

    blobmsg_parse(delayed_echo_policy, ARRAY_SIZE(delayed_echo_policy), tb,
            blob_data(msg), blob_len(msg));
    char *message = NULL;
    uint32_t delay;

    if(!tb[DELAYED_ECHO_MESSAGE]) {
        beep_reply_error(ctx, req, "Missing parameter: message", 0);
        return 0;
    }
    message = blobmsg_data(tb[DELAYED_ECHO_MESSAGE]);

    if(!tb[DELAYED_ECHO_DELAY]) {
        beep_reply_error(ctx, req, "Missing parameter: delay", 0);
        return 0;
    }

    delay = blobmsg_get_u32(tb[DELAYED_ECHO_DELAY]);

    struct delayed_echo_req *held_req =
            calloc(1, sizeof(struct delayed_echo_req));
    ubus_defer_request(ctx, req, &held_req->req);
    held_req->ctx = ctx;
    held_req->message = strdup(message);
    held_req->timeout.cb = delayed_echo_timeout;

    uloop_timeout_set(&held_req->timeout, delay);
    return 0;
}

/*
 * ubus method: events(n, interval, size)
 */

enum {
    EVENTS_N,
    EVENTS_INTERVAL,
    EVENTS_SIZE,
    __EVENTS_MAX
};

const struct blobmsg_policy events_policy[] = {
    [EVENTS_N] = { .name = "n", .type = BLOBMSG_TYPE_INT32 },
    [EVENTS_INTERVAL] = { .name = "interval", .type = BLOBMSG_TYPE_INT32 },
    [EVENTS_SIZE] = { .name = "size", .type = BLOBMSG_TYPE_INT32 },
};

struct event_generator {
    struct ubus_context *ctx;
    struct uloop_timeout timeout;

    uint32_t n;
    uint32_t interval;
    uint32_t size;
};

static void create_random_data(char *buf, size_t size) {
    for(int i=0;i<size-1;i++) {
        buf[i] = 48 + (rand() % 32);
    }

    buf[size-1] = 0;
}

static void generate_event(struct uloop_timeout *t) {
    struct event_generator *eg =
            container_of(t, struct event_generator, timeout);
    if(eg->n == 0) {
        free(eg);
        return;
    } else {
        eg->n--;
    }

    char *data = malloc(eg->size);
    create_random_data(data, eg->size);

    struct blob_buf buf;
    memset(&buf, 0, sizeof(struct blob_buf));
    blob_buf_init(&buf, 0);
    blobmsg_add_string(&buf, "data", data);

    ubus_send_event_async(eg->ctx, "uhammer.dummy_event", buf.head);

    blob_buf_free(&buf);
    free(data);

    uloop_timeout_set(&eg->timeout, eg->interval);
};

static int hammer_events(
        struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    struct blob_attr *tb[__EVENTS_MAX];

    blobmsg_parse(events_policy, ARRAY_SIZE(events_policy), tb,
            blob_data(msg), blob_len(msg));

    uint32_t n, interval, size;

    if(!tb[EVENTS_N]) {
        beep_reply_error(ctx, req, "Missing parameter: n", 0);
        return 0;
    }
    n = blobmsg_get_u32(tb[EVENTS_N]);

    if(!tb[EVENTS_INTERVAL]) {
        beep_reply_error(ctx, req, "Missing parameter: interval", 0);
        return 0;
    }
    interval = blobmsg_get_u32(tb[EVENTS_INTERVAL]);

    if(!tb[EVENTS_SIZE]) {
        beep_reply_error(ctx, req, "Missing parameter: size", 0);
        return 0;
    }
    size = blobmsg_get_u32(tb[EVENTS_SIZE]);

    struct event_generator *eg =
            calloc(1, sizeof(struct event_generator));
    eg->ctx = ctx;
    eg->timeout.cb = generate_event;
    eg->n = n;
    eg->interval = interval;
    eg->size = size;

    uloop_timeout_set(&eg->timeout, 0);

    beep_reply_success(ctx, req, NULL);
    return 0;
}

/*
 * ubus method: listen_for(event)
 *
 * Registers an event listen that listens for <event>
 */

enum {
    LISTEN_FOR_EVENT,
    __LISTEN_FOR_MAX
};

const struct blobmsg_policy listen_for_policy[] = {
    [LISTEN_FOR_EVENT] = { .name = "event", .type = BLOBMSG_TYPE_STRING },
};

static void generic_event_handler(struct ubus_context *ctx,
        struct ubus_event_handler *ev, const char *type,
        struct blob_attr *msg) {
    LOG_INFO(log_beep_main, "* %s *", type);
    return;
}

struct ubus_event_handler ev = {
    .cb = &generic_event_handler,
};

static int hammer_listen_for(
        struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    struct blob_attr *tb[__ECHO_MAX];

    blobmsg_parse(listen_for_policy, ARRAY_SIZE(listen_for_policy), tb,
            blob_data(msg), blob_len(msg));
    char *event = NULL;

    if(!tb[LISTEN_FOR_EVENT]) {
        beep_reply_error(ctx, req, "Missing parameter: event", 0);
        return 0;
    }
    event = blobmsg_data(tb[LISTEN_FOR_EVENT]);

    LOG_INFO(log_beep_main, "Adding event handler for %s", event);
    ubus_register_event_handler_async(ctx, &ev, event, NULL, NULL);

    beep_reply_success(ctx, req, NULL);
    return 0;
}

/*
 * ubus method: spawn_dummy(name)
 *
 * spawns a dummy ubus object that removes itself after ttl
 */

/*
 * ubus dummy object definition
 */

static const struct ubus_method dummy_methods[] = {
    UBUS_METHOD_NOARG("nothing", NULL),
};

struct ubus_object_type dummy_type =
        UBUS_OBJECT_TYPE(NULL, dummy_methods);

struct ubus_object dummy_object = {
    .name = NULL,
    .type = &dummy_type,
    .methods = NULL,
    .n_methods = 0,
};

struct dummy {
    struct ubus_context *ctx;
    struct ubus_request_data req;
    struct uloop_timeout timeout;
    struct ubus_object obj;

    char name[128];
    uint32_t ttl;
};

enum {
    SPAWN_DUMMY_TTL,
    __SPAWN_DUMMY_MAX
};

const struct blobmsg_policy spawn_dummy_policy[] = {
    [SPAWN_DUMMY_TTL] = { .name = "ttl", .type = BLOBMSG_TYPE_INT32 },
};

static void dummy_removed_cb(void *priv) {
    struct dummy *d = (struct dummy *)priv;

    LOG_INFO(log_beep_main, "Dummy object %s removed", d->name);
    free(d);
}

static void dummy_ttl_cb(struct uloop_timeout *t) {
    struct dummy *d =
        container_of(t, struct dummy, timeout);

    ubus_remove_object_async(d->ctx, &d->obj, &dummy_removed_cb, d);
}

static void dummy_added_cb(
        struct ubus_context *ctx, struct ubus_object *obj, void *priv) {
    struct dummy *d = (struct dummy *)priv;
    uloop_timeout_set(&d->timeout, d->ttl);
    LOG_INFO(log_beep_main, "Dummy object %s added", d->name);
    beep_reply_success(ctx, &d->req, NULL);
    ubus_complete_deferred_request(ctx, &d->req, 0);
}

static void create_dummy_name(char *buf, size_t len) {
    char rand_stuff[16];
    for(int i=0;i < 15;i++) {
        rand_stuff[i] = 97 + (rand() % 26);
    }
    rand_stuff[15] = '\0';
    snprintf(buf, len, "uhammer_%s", rand_stuff);
}

static int hammer_spawn_dummy(
        struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    struct blob_attr *tb[__EVENTS_MAX];

    blobmsg_parse(spawn_dummy_policy, ARRAY_SIZE(spawn_dummy_policy), tb,
            blob_data(msg), blob_len(msg));

    uint32_t ttl;

    if(!tb[SPAWN_DUMMY_TTL]) {
        beep_reply_error(ctx, req, "Missing parameter: ttl", 0);
        return 0;
    }
    ttl = blobmsg_get_u32(tb[SPAWN_DUMMY_TTL]);

    struct dummy *d = calloc(1, sizeof(struct dummy));
    d->ctx = ctx;
    create_dummy_name(d->name, 128);
    d->ttl = ttl;

    d->obj.name = d->name;
    d->obj.type = &dummy_type;
    d->obj.methods = dummy_methods;
    d->obj.n_methods = 1;

    d->timeout.cb = &dummy_ttl_cb;

    int ret = ubus_add_object_async(ctx, &d->obj, &dummy_added_cb, d);
    if(ret) {
        beep_reply_error(ctx, req, "Failed to add object", ret);
        free(d);
    }

    ubus_defer_request(ctx, req, &d->req);
    return 0;
}

/*
 * ubus object definition
 */

static const struct ubus_method hammer_methods[] = {
    UBUS_METHOD("echo", hammer_echo, echo_policy),
    UBUS_METHOD("blocking_echo", hammer_blocking_echo, blocking_echo_policy),
    UBUS_METHOD("delayed_echo", hammer_delayed_echo, delayed_echo_policy),
    UBUS_METHOD("events", hammer_events, events_policy),
    UBUS_METHOD("listen_for", hammer_listen_for, listen_for_policy),
    UBUS_METHOD("spawn_dummy", hammer_spawn_dummy, spawn_dummy_policy),
};

struct ubus_object_type hammer_type =
        UBUS_OBJECT_TYPE(NULL, hammer_methods);

struct ubus_object hammer_object = {
    .name = NULL,
    .type = &hammer_type,
    .methods = hammer_methods,
    .n_methods = ARRAY_SIZE(hammer_methods),
};

/*
 * auto event generator
 */

struct auto_events {
    struct ubus_context *ctx;
    struct uloop_timeout t;

    char prefix[32];
    int num_events;
    int event_interval;
};

static void auto_events_cb(struct uloop_timeout *t) {
    struct auto_events *ae = container_of(t, struct auto_events, t);

    LOG_INFO(log_beep_main, "(auto event iteration)");

    char id_buf[40];
    struct blob_buf buf;
    memset(&buf, 0, sizeof(struct blob_buf));
    blob_buf_init(&buf, 0);

    for(int i=0; i < ae->num_events; i++) {
        snprintf(id_buf, 40, "%s%d", ae->prefix, i);
        ubus_send_event_async(ae->ctx, id_buf, buf.head);
    }

    uloop_timeout_set(&ae->t, ae->event_interval);
}

static struct auto_events *setup_dummy_events(struct ubus_context *ctx) {
    if(uhammer_flags.num_events < 1 || uhammer_flags.event_interval < 1) {
        LOG_INFO(log_beep_main, "Automatic dummy event generator disabled");
        LOG_INFO(log_beep_main, "(To enable, need num_events > 0 and event_interval > 0)");
        return NULL;
    }

    struct auto_events *ae = calloc(1, sizeof(struct auto_events));

    ae->ctx = ctx;
    ae->t.cb = &auto_events_cb;
    ae->num_events = uhammer_flags.num_events;
    ae->event_interval = uhammer_flags.event_interval;
    snprintf(ae->prefix, 32, "%s.dummy_event", uhammer_flags.obj_name);

    uloop_timeout_set(&ae->t, 0);
    return ae;
}

/*
 * entry point
 */

int main(int argc, char *argv[]) {
    log_beep_main = LOG_CATEGORY_GET("uhammer");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    beep_flags_init(argc, argv);

    log_beep_main = LOG_CATEGORY_GET(uhammer_flags.obj_name);

    srand(time(NULL));

    uloop_init();
    struct ubus_context *ctx = beep_ubus_connect(uhammer_flags.obj_name);
    if(!ctx) {
        LOG_ERROR(log_beep_main, "Failed to connect to ubus");
        return -1;
    }
    ubus_add_uloop(ctx);

    hammer_object.name = uhammer_flags.obj_name;

    int ret = ubus_add_object(ctx, &hammer_object);
    if(ret) {
        LOG_ERROR(log_beep_main, "Failed to add ubus object: %s\n",
                ubus_strerror(ret));
        return -1;
    }

    struct auto_events *ae = setup_dummy_events(ctx);

    uloop_run();
    ubus_free(ctx);
    uloop_done();
    free(ae);
    return 0;
}
