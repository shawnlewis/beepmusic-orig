#define _GNU_SOURCE

#include "beep_ubus.h"

#include <pthread.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "debug.h"
#include "flags.h"
#include "libubox/uloop.h"

///// Flags

// This is an app-wide global, available via debug.h
struct flag_vals {
    char* ubus;
};

// With default values;
struct flag_vals ubus_flags = {
    .ubus = NULL
};

static const BeepFlag flags[] = {
    BEEP_FLAG("ubus", BEEP_FLAG_STRING, &ubus_flags.ubus, NULL, NULL)
};

BEEP_INIT_FLAGS(flags);

///// End Flags

static const char* safe_ubus_invoke_path;
static const char* safe_ubus_invoke_method;
static struct blob_attr* safe_ubus_invoke_msg;
static struct blob_attr** safe_ubus_invoke_response;
static int safe_ubus_invoke_ret;

static struct ubus_context *_ctx;
static pthread_key_t is_ubus_thread_key;
static pthread_mutex_t safe_call_mutex;
static pthread_mutex_t safe_cond_mutex;
static pthread_cond_t safe_cond;

#define AGENT_NAME_MAX_LEN 128

#define INVOKE_TIMEOUT 21000

static char _agent_name[AGENT_NAME_MAX_LEN];

static bool is_ubus_thread(void) {
    return (bool) pthread_getspecific(is_ubus_thread_key);
}

static void pong_done_cb(
        int ubus_ret, struct blob_attr* response, void* priv) {
    if (ubus_ret) {
        LOG_WARN(log_beep_main, "Sending pong failed. (%d)", ubus_ret);
    }
}

static void _on_ping_cb(
        struct ubus_context *ctx, struct ubus_event_handler *ev,
        const char *type, struct blob_attr *msg) {
    static struct blob_buf buf;

    blob_buf_init(&buf, 0);
    blobmsg_add_string(&buf, "agent", _agent_name);
    ubus_invoke_async_full(ctx, "beep.health", "pong", buf.head, 2000,
            pong_done_cb, NULL);
    blob_buf_free(&buf);
}

static struct ubus_event_handler _on_ping_obj = {
    .cb = _on_ping_cb
};

static void invoke_async_done(
        int ubus_ret, struct blob_attr *response, void* priv) {
    safe_ubus_invoke_ret = ubus_ret;
    if (!ubus_ret) {
        if (response) {
            *safe_ubus_invoke_response = blob_memdup(response);
        } else {
            LOG_ERROR(log_beep_main,
                    "Unexpected condition: got ubus_ret == 0 "
                    "but no response. Aborting...");
            abort();
        }
    }

    pthread_cond_signal(&safe_cond);
    pthread_mutex_unlock(&safe_cond_mutex);
}

static void on_event_invoke(struct uloop_fd *u, unsigned int events) {
    uint64_t val;

    pthread_mutex_lock(&safe_cond_mutex);

    read(u->fd, &val, 8);

    *safe_ubus_invoke_response = NULL;

    // TODO: use timeout
    beep_ubus_invoke_async(safe_ubus_invoke_path, safe_ubus_invoke_method,
            safe_ubus_invoke_msg, INVOKE_TIMEOUT, invoke_async_done, NULL);
}

struct uloop_fd uloop_fd_invoke = {
    .cb = on_event_invoke
};

static void on_event_send(struct uloop_fd *u, unsigned int events) {
    printf("GOT SEND\n");
    uint64_t val;
    read(u->fd, &val, 8);
}

struct uloop_fd uloop_fd_send = {
    .cb = on_event_send
};

int other_thread_ubus_invoke(
        const char* path, const char* method, struct blob_attr* msg,
        struct blob_attr** response) {
    int ret;

    // We use a separate lock (safe_call_mutex) so that if there are
    // multiple non ubus threads only one may run a ubus method at a time
    // (therefore pthread_cond_signal will trigger the one and only thread
    // that may be waiting).
    pthread_mutex_lock(&safe_call_mutex);
    pthread_mutex_lock(&safe_cond_mutex);

    // Setup args
    safe_ubus_invoke_path = path;
    safe_ubus_invoke_method = method;
    safe_ubus_invoke_msg = msg;
    safe_ubus_invoke_response = response;

    // Trigger event
    uint64_t val = 1;
    write(uloop_fd_invoke.fd, &val, 8);

    // Wait for completion
    pthread_cond_wait(&safe_cond, &safe_cond_mutex);

    ret = safe_ubus_invoke_ret;

    pthread_mutex_unlock(&safe_cond_mutex);
    pthread_mutex_unlock(&safe_call_mutex);

    return ret;
}

static void ubus_thread_ubus_invoke_cb(
        struct ubus_request *req, int type, struct blob_attr *msg) {
    struct blob_attr** response = req->priv;

    // Read response
    int response_len = blob_pad_len(msg);
    *response = malloc(response_len);
    memcpy(*response, msg, response_len);
}

int ubus_thread_ubus_invoke(
        const char* path, const char* method, struct blob_attr* msg,
        struct blob_attr** response) {
    uint32_t id;
    int ret = ubus_lookup_id(_ctx, path, &id);
    if (ret) {
        return ret;
    }

    return ubus_invoke(
            _ctx, id, method, msg,
            ubus_thread_ubus_invoke_cb, response, INVOKE_TIMEOUT);
}

// Caller owns returned response and must free it.
int beep_ubus_invoke(
        const char* path, const char* method, struct blob_attr* msg,
        struct blob_attr** response) {
    *response = NULL;
    if (is_ubus_thread()) {
        return ubus_thread_ubus_invoke(path, method, msg, response);
    } else {
        return other_thread_ubus_invoke(path, method, msg, response);
    }
}

void beep_ubus_invoke_async(
        const char* path, const char* method, struct blob_attr* msg,
        uint32_t timeout_msecs,
        ubus_invoke_async_handler_t callback, void* priv) {
    if (is_ubus_thread()) {
        ubus_invoke_async_full(_ctx, path, method, msg, timeout_msecs,
                callback, priv);
    } else {
        LOG_ERROR(log_beep_main,
                "beep_ubus_invoke_async must be called from "
                "ubus thread. Aborting...");
        abort();
    }
}

static int ubus_thread_ubus_send_event(const char *id, struct blob_attr *data) {
    return ubus_send_event_async(_ctx, id, data);
}

int beep_ubus_send_event(
        struct ubus_context *ctx, const char* id, struct blob_attr* data) {
    if (is_ubus_thread()) {
        return(ubus_thread_ubus_send_event(id, data));
    } else {
        LOG_ERROR(log_beep_main, "BEEP_UBUS_SEND_EVENT FROM NON UBUS THREAD");
        abort();
    }
}

void beep_ubus_disconnect(struct ubus_context* ctx) {
    pthread_mutex_lock(&safe_cond_mutex);

    uloop_fd_delete(&uloop_fd_send);
    close(uloop_fd_send.fd);

    uloop_fd_delete(&uloop_fd_invoke);
    close(uloop_fd_invoke.fd);

    pthread_mutex_unlock(&safe_cond_mutex);

    if (!_ctx || _ctx != ctx)
        return;

    ubus_unregister_event_handler_async(_ctx, &_on_ping_obj, NULL, NULL);
    ubus_free(_ctx);
    _ctx = NULL;
}


struct ubus_context* beep_ubus_connect(const char *agent_name) {
    static bool _connected = false;

    if (_connected) {
        LOG_ERROR(log_beep_main, "beep_ubus_connect called twice");
        abort();
    }

    _connected = true;

    pthread_key_create(&is_ubus_thread_key, NULL);
    pthread_setspecific(is_ubus_thread_key, (void*) 1);
    pthread_mutex_init(&safe_call_mutex, NULL);
    pthread_mutex_init(&safe_cond_mutex, NULL);
    pthread_cond_init(&safe_cond, NULL);

    _ctx = ubus_connect(ubus_flags.ubus);
    strncpy(_agent_name, agent_name, AGENT_NAME_MAX_LEN);

    if (!_ctx) {
        char* ubus_loc = "default";
        if (ubus_flags.ubus) {
            ubus_loc = ubus_flags.ubus;
        }
        LOG_ERROR(log_beep_main,
                "Failed to connect to ubus: %s", ubus_loc);
        exit(1);
    }

    ubus_register_event_handler_async(
            _ctx, &_on_ping_obj, "beep.ping", NULL, NULL);

    pthread_mutex_lock(&safe_cond_mutex);

    uloop_fd_invoke.fd = eventfd(0, 0);
    uloop_fd_add(&uloop_fd_invoke, ULOOP_READ);

    uloop_fd_send.fd = eventfd(0, 0);
    uloop_fd_add(&uloop_fd_send, ULOOP_READ);

    pthread_mutex_unlock(&safe_cond_mutex);

    return _ctx;
}

struct ubus_context* beep_ubus_get_ctx(void) {
    return _ctx;
}

const char *beep_ubus_get_agent_name(void) {
    return _agent_name;
}

void beep_send_state(
        struct ubus_context *ctx, const char *component,
        const char *event_type, struct blob_attr *event_data,
        struct blob_attr *state) {
    static struct blob_buf buf;
    struct blob_attr *cur;
    char id[BEEP_UBUS_EVENT_ID_MAX_LENGTH];
    int rem;
    void *t;

    if(!component || !event_type || !event_data || !state) {
        LOG_ERROR(log_beep_main,
                "beep_send_state requires all non-null params");
        abort();
    }


    // Skip initial "beep." if present
    int skip_component_chars = 0;
    if (strstr(component, "beep.")) {
        skip_component_chars = 5;
    }
    snprintf(id, BEEP_UBUS_EVENT_ID_MAX_LENGTH, "beep.state.%s._local_",
            component + skip_component_chars);

    blob_buf_init(&buf, 0);
    blobmsg_add_string(&buf, "event_type", event_type);

    // We need a sequence number if components are going to listen
    // to other components directly (so that they can fetch the initial
    // state and resolve the potential race that ensues). We could
    // fetch the initial state from state_proxy, which has state for
    // all current components.
    //blobmsg_add_u32(&buf, "seq", seq++);

    t = blobmsg_open_table(&buf, "event_data");
    blob_for_each_attr(cur, event_data, rem) {
        blobmsg_add_blob(&buf, cur);
    }
    blobmsg_close_table(&buf, t);

    t = blobmsg_open_table(&buf, "state");
    blob_for_each_attr(cur, state, rem) {
        blobmsg_add_blob(&buf, cur);
    }
    blobmsg_close_table(&buf, t);

    int ret = beep_ubus_send_event(_ctx, id, buf.head);
    if(ret) {
        LOG_ERROR(log_beep_main, "ubus_send_event failed: %s (is ubusd dead?)",
                ubus_strerror(ret));
        return;
    }
}

void beep_reply_success(
        struct ubus_context* ctx, struct ubus_request_data* req,
        struct blob_attr* result) {
    static struct blob_buf buf;
    blob_buf_init(&buf, 0);
    blobmsg_add_u8(&buf, "success", true);
    void* r = blobmsg_open_table(&buf, "result");
    struct blob_attr *cur;
    int rem;

    if (result) {
        blob_for_each_attr(cur, result, rem) {
            blobmsg_add_blob(&buf, cur);
        }
    }
    blobmsg_close_table(&buf, r);
    int ret = ubus_send_reply(ctx, req, buf.head);
    if(ret) {
        LOG_ERROR(log_beep_main, "ubus_send_reply failed: %s (is ubusd dead?)",
                ubus_strerror(ret));
        return;
    }
}

void beep_reply_error(
        struct ubus_context* ctx, struct ubus_request_data* req,
        const char* error_msg, const uint32_t error_code) {
    static struct blob_buf buf;
    blob_buf_init(&buf, 0);
    blobmsg_add_u8(&buf, "success", false);
    blobmsg_add_u32(&buf, "error_code", error_code);
    blobmsg_add_string(&buf, "error_message", error_msg);
    int ret = ubus_send_reply(ctx, req, buf.head);
    if(ret) {
        LOG_ERROR(log_beep_main, "ubus_send_reply failed: %s (is ubusd dead?)",
                ubus_strerror(ret));
        return;
    }
}

static const struct blobmsg_policy beep_response_policy[] = {
    [BEEP_RESPONSE_SUCCESS] = { .name = "success", .type = BLOBMSG_TYPE_BOOL },
    [BEEP_RESPONSE_RESULT] = { .name = "result", .type = BLOBMSG_TYPE_UNSPEC },
    [BEEP_RESPONSE_ERROR_CODE] =
            { .name = "error_code", .type = BLOBMSG_TYPE_INT32 },
    [BEEP_RESPONSE_ERROR_MESSAGE] =
            { .name = "error_message", .type = BLOBMSG_TYPE_STRING },
};

bool beep_parse_response(
        const struct blob_attr* response,
        struct blob_attr *parsed[__BEEP_RESPONSE_MAX]) {
    //printf("Response: %s\n", blobmsg_format_json_indent((struct blob_attr*) response, true, 0));
    blobmsg_parse(beep_response_policy, __BEEP_RESPONSE_MAX,
            parsed, blob_data(response), blob_len(response));
    if (!parsed[BEEP_RESPONSE_SUCCESS]) {
        LOG_ERROR(log_beep_main,
                "beep ubus response didn't include \"success\" field\n");
        return false;
    }
    bool success = blobmsg_get_bool(parsed[BEEP_RESPONSE_SUCCESS]);
    if (success) {
        if (!parsed[BEEP_RESPONSE_RESULT]) {
            LOG_ERROR(log_beep_main,
                "beep successful ubus response didn't include \"result\" "
                "field\n");
            return false;
        }
    } else {
        if (!parsed[BEEP_RESPONSE_ERROR_CODE]) {
            LOG_ERROR(log_beep_main,
                "beep error ubus response didn't include \"error_code\" "
                "field\n");
            return false;
        }
        if (!parsed[BEEP_RESPONSE_ERROR_MESSAGE]) {
            LOG_ERROR(log_beep_main,
                "beep error ubus response didn't include \"error_message\" "
                "field\n");
            return false;
        }
    }
    return true;

}

void beep_host_to_dev_id(char *id, const char *host, int port) {
    char *c;
    strncpy(id, host, BEEP_UBUS_DEV_ID_MAX_LENGTH);
    while((c = strchr(id,'.'))) {
        *c = '_';
    }

    int id_len = strlen(id);
    snprintf(id + id_len, BEEP_UBUS_DEV_ID_MAX_LENGTH - id_len,
            "__%d", port);
}

struct beep_subscription {
    /* state updates */
    struct ubus_event_handler ubus_handler;

    /* get_state */
    struct ubus_request req;

    char component[64]; // TODO: This is arbitrarily long -- what is the real
                        // max length of a component name?
    beep_event_handler_t callback;
    void *userdata;
    bool has_initial_state;
    bool handler_installed;
};

static void common_state_handler(struct beep_subscription *s,
        struct blob_attr *state, const char *event_type,
        struct blob_attr *event_data) {
    s->callback(s->component, state,
            s->has_initial_state ? event_data : NULL,
            s->has_initial_state ? event_type : NULL);
    s->has_initial_state = true;
}

static void beep_subscription_event_handler(struct ubus_context *ctx,
        struct ubus_event_handler *ev, const char *type,
        struct blob_attr *msg) {
    struct beep_subscription *s = container_of(ev, struct beep_subscription,
            ubus_handler);
    struct blob_attr *tb_event[__BEEP_STATE_MAX];
    struct blob_attr *state, *event_data;
    const char *event_type;

    if(blobmsg_parse(beep_state_policy, __BEEP_STATE_MAX, tb_event,
                blob_data(msg), blob_len(msg))) {
        LOG_WARN(log_beep_main, "Failed to parse top level event for %s",
                type);
        abort();
    }

    if(tb_event[BEEP_STATE_STATE]) {
        state = tb_event[BEEP_STATE_STATE];
    } else {
        LOG_ERROR(log_beep_main, "No state field found in event object for %s",
                type);
        abort();
    }

    if(tb_event[BEEP_STATE_EVENT_TYPE]) {
        event_type = blobmsg_get_string(tb_event[BEEP_STATE_EVENT_TYPE]);
    } else {
        event_type = NULL;
    }

    if(tb_event[BEEP_STATE_EVENT_DATA]) {
        event_data = blobmsg_data(tb_event[BEEP_STATE_EVENT_DATA]);
    } else {
        event_data = NULL;
    }

    common_state_handler(s, state, event_type, event_data);
}

static void beep_subscription_invoke_cb(int ubus_ret,
        struct blob_attr *response, void *priv) {
    struct beep_subscription *s =
        (struct beep_subscription *)priv;

    if(ubus_ret) {
        LOG_ERROR(log_beep_main, "Error completing beep.%s::get_state: %s",
                s->component, ubus_strerror(ubus_ret));
        return;
    }

    s->handler_installed = true;

    struct blob_attr *resp[__BEEP_RESPONSE_MAX];
    if(!beep_parse_response(response, resp)) {
        LOG_ERROR(log_beep_main, "Error parsing response.  Aborting...");
        abort();
    }

    struct blob_attr *state = resp[BEEP_RESPONSE_RESULT];
    common_state_handler(s, state, NULL, NULL);
}

static void subscribe_register_event_handler_cb(void *priv) {
    char *path;
    struct beep_subscription *s = (struct beep_subscription *)priv;

    if(asprintf(&path,"beep.%s", s->component) < 0) {
        LOG_ERROR(log_beep_main, "Couldn't allocate invoke path");
        abort();
    }

    ubus_invoke_async_full(_ctx, path, "get_state", NULL, 5000,
            beep_subscription_invoke_cb, s);
    free(path);
}

struct beep_subscription *beep_ubus_subscribe(const char *component, beep_event_handler_t
        callback, void *userdata) {
    if(!component || !callback) {
        LOG_ERROR(log_beep_main, "Programming error: component and callback may not be null");
        abort();
    }

    struct beep_subscription *s = calloc(1, sizeof(struct beep_subscription));
    s->ubus_handler.cb = beep_subscription_event_handler;
    strncpy(s->component, component, 64);
    s->callback =callback;
    s->userdata = userdata;

    // Set up event listener
    char *path;
    if(asprintf(&path,"beep.state.%s._local_", component) < 0) {
        LOG_ERROR(log_beep_main, "Couldn't allocate event path");
        abort();
    }

    ubus_register_event_handler_async(_ctx, &s->ubus_handler, path,
            subscribe_register_event_handler_cb, s);
    free(path);

    return s;
}

struct beep_ubus_unsubscribe_obj {
    struct beep_subscription *s;

    beep_ubus_unsubscribe_complete_handler_t callback;
    void *priv;
};

// ubus_remove_object_handler_T
static void unregister_event_handler_callback(void *priv) {
    struct beep_ubus_unsubscribe_obj *obj =
        (struct beep_ubus_unsubscribe_obj *)priv;

    obj->s->handler_installed = false;

    if(obj->callback) {
        obj->callback(obj->priv);
    }

    free(obj->s);
    free(obj);
}

void beep_ubus_unsubscribe(struct beep_subscription *s,
        beep_ubus_unsubscribe_complete_handler_t callback, void *priv) {
    struct beep_ubus_unsubscribe_obj *obj;
    if(!s) {
        return;
    }

    obj = calloc(1, sizeof(struct beep_ubus_unsubscribe_obj));
    obj->s = s;
    obj->callback = callback;
    obj->priv = priv;

    if(s->handler_installed) {
        ubus_unregister_event_handler_async(_ctx, &s->ubus_handler,
                &unregister_event_handler_callback, obj);
    }
}
