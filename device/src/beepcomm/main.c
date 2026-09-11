#include "beepcomm.h"

bool debug = false;

extern int ubus_complete_request_allowed;

struct flag_vals comm_flags = {
    .ssdp_port = SSDP_PORT,
    .dial_port = DIAL_PORT,
    .msg_port = MSG_PORT,
    .debug = false,
    .ubus_stub = false,
    .autostart = false
};

static BeepFlag flags[] = {
    BEEP_FLAG("ssdp_port", BEEP_FLAG_INT, &comm_flags.ssdp_port, NULL, NULL),
    BEEP_FLAG("dial_port", BEEP_FLAG_INT, &comm_flags.dial_port, NULL, NULL),
    BEEP_FLAG("msg_port", BEEP_FLAG_INT, &comm_flags.msg_port, NULL, NULL),
    BEEP_FLAG("debug", BEEP_FLAG_BOOL, &comm_flags.debug, NULL, NULL),
    BEEP_FLAG("ubus_stub", BEEP_FLAG_BOOL, &comm_flags.ubus_stub, NULL, NULL),
    BEEP_FLAG("autostart", BEEP_FLAG_BOOL, &comm_flags.autostart, NULL, NULL),
};

BEEP_INIT_FLAGS(flags)


static struct ubus_context *ubus_ctx = NULL;
static struct beep_subscription *manager_sub = NULL;
static struct messages_context *messages_ctx = NULL;
static struct apps_context *apps_ctx = NULL;
static struct ssdp_context *ssdp_ctx = NULL;
static char uuid[40] = {0,};

static char *get_group_label_from_manager_state(struct blob_attr **manager) {
    struct blob_attr *local_device[__MANAGER_LOCAL_MAX];

    if(!manager[MANAGER_STATE_LOCAL_DEVICE]) {
        LOG_ERROR(log_beep_main, "Manager state missing local device table");
        abort();
    }

    if(blobmsg_parse(manager_local_policy, __MANAGER_LOCAL_MAX,
                local_device, blobmsg_data(manager[MANAGER_STATE_LOCAL_DEVICE]),
                blobmsg_data_len(manager[MANAGER_STATE_LOCAL_DEVICE]))) {
        LOG_ERROR(log_beep_main, "Failed to parse local device");
        abort();
    }

    if(!local_device[MANAGER_LOCAL_GROUP_LABEL]) {
        LOG_ERROR(log_beep_main, "Local device table missing group_label element");
        abort();
    }

    return strdup(blobmsg_get_string(local_device[MANAGER_LOCAL_GROUP_LABEL]));
}

static void get_uuid_from_manager_state(struct blob_attr **manager, char *uuid) {
    struct blob_attr *local_device[__MANAGER_LOCAL_MAX];

    if(!manager[MANAGER_STATE_LOCAL_DEVICE]) {
        LOG_ERROR(log_beep_main, "Manager state missing local device table");
        abort();
    }

    if(blobmsg_parse(manager_local_policy, __MANAGER_LOCAL_MAX,
                local_device, blobmsg_data(manager[MANAGER_STATE_LOCAL_DEVICE]),
                blobmsg_data_len(manager[MANAGER_STATE_LOCAL_DEVICE]))) {
        LOG_ERROR(log_beep_main, "Failed to parse local device");
        abort();
    }

    if(!local_device[MANAGER_LOCAL_GROUP_LABEL]) {
        LOG_ERROR(log_beep_main, "Local device table missing group_label element");
        abort();
    }

    memcpy(uuid, blobmsg_get_string(local_device[MANAGER_LOCAL_SET_UUID]), 37);
}

static void manager_event_handler(const char *component,
        struct blob_attr *state, struct blob_attr *event_data,
        const char *event_type) {
    struct blob_attr *manager[__MANAGER_STATE_MAX];
    char uuid[37];

    if(!ssdp_ctx) {
        return;
    }

    if(blobmsg_parse(manager_state_policy, __MANAGER_STATE_MAX,
                manager, blobmsg_data(state), blobmsg_data_len(state))) {
        LOG_ERROR(log_beep_main, "Failed to parse manager state");
        abort();
    }

    char *friendly_name = get_group_label_from_manager_state(manager);
    get_uuid_from_manager_state(manager, uuid);

    pthread_mutex_lock(&ssdp_ctx->lock);

        // Update friendly name
        free(ssdp_ctx->friendly_name);
        ssdp_ctx->friendly_name = friendly_name;

        // Update uuid
        memcpy(ssdp_ctx->uuid, uuid, 37);

        // Update last_updated
        time_t now = time(0);
        struct tm tm = *gmtime(&now);

        ssdp_ctx->friendly_name = strdup(friendly_name);
        strftime(ssdp_ctx->last_updated, sizeof(ssdp_ctx->last_updated),
                "%a, %d %b %Y %H:%M:%S %Z", &tm);

    pthread_mutex_unlock(&ssdp_ctx->lock);
}

static void messages_up(struct ubus_request_data *req) {
    char friendly_name[100];

    if(!apps_ctx) {
        apps_ctx = start_apps_api();
    }

    if(!ssdp_ctx) {
        int ret = beep_config_device_read("device_name", friendly_name, 100);
        if(ret == -1) {
            LOG_WARN(log_beep_main, "Device name not found; using default 'unnamed'");
            strncpy(friendly_name, "Unknown Beep", 100);
        }
        ssdp_ctx = start_ssdp(friendly_name, "Beep Model 001", uuid);
    }

    if(req) {
        beep_reply_success(ubus_ctx, req, NULL);
        ubus_complete_deferred_request(ubus_ctx, req, 0);
        free(req);
    }
}

static void start_messages_cb(struct messages_context *ctx, void *priv) {
    struct ubus_request_data *req = (struct ubus_request_data *)priv;

    messages_ctx = ctx;
    messages_up(req);

}

static void _start(struct ubus_request_data *req) {
    manager_sub = beep_ubus_subscribe("manager", manager_event_handler, NULL);

    if(!messages_ctx) {
        start_messages(ubus_ctx, start_messages_cb, req);
    } else {
        messages_up(req);
    }
}

static int start(struct ubus_context *ctx,
        struct ubus_object *obj, struct ubus_request_data *req,
        const char *method, struct blob_attr *msg) {
    struct ubus_request_data *deferred_req = calloc(1, sizeof(struct ubus_request_data));
    ubus_defer_request(ctx, req, deferred_req);
    _start(deferred_req);
    return 0;
}

static void messages_down(struct ubus_request_data *req) {
    if(req) {
        beep_reply_success(ubus_ctx, req, NULL);
        ubus_complete_deferred_request(ubus_ctx, req, 0);
        free(req);
    }
}

static void stop_messages_cb(void *priv) {
    struct ubus_request_data *req = (struct ubus_request_data *)priv;

    messages_ctx = NULL;
    messages_down(req);

}

static void unsubscribe_complete(void *priv) {
    manager_sub = NULL;

    if(ssdp_ctx) {
        stop_ssdp(ssdp_ctx);
        ssdp_ctx = NULL;
    }

    if(apps_ctx) {
        stop_apps_api(apps_ctx);
        apps_ctx = NULL;
    }

    if(messages_ctx) {
        stop_messages(messages_ctx, stop_messages_cb, priv);
    } else {
        messages_down((struct ubus_request_data *)priv);
    }
}

static void _stop(struct ubus_request_data *req) {
    if(manager_sub) {
        beep_ubus_unsubscribe(manager_sub, unsubscribe_complete, req);
    } else {
        beep_reply_success(ubus_ctx, req, NULL);
        ubus_complete_deferred_request(ubus_ctx, req, 0);
        free(req);
    }
}

static int stop(struct ubus_context *ctx,
        struct ubus_object *obj, struct ubus_request_data *req,
        const char *method, struct blob_attr *msg) {
    struct ubus_request_data *deferred_req = calloc(1, sizeof(struct ubus_request_data));
    ubus_defer_request(ctx, req, deferred_req);

    _stop(deferred_req);

    return 0;
}

static const struct ubus_method comm_control_methods[] = {
    UBUS_METHOD_NOARG("start", start),
    UBUS_METHOD_NOARG("stop", stop),
};

static struct ubus_object_type comm_control_type =
    UBUS_OBJECT_TYPE(NULL, comm_control_methods);

static struct ubus_object comm_control_object = {
    .name = NULL,
    .type = &comm_control_type,
    .methods = comm_control_methods,
    .n_methods = ARRAY_SIZE(comm_control_methods)
};

int main(int argc, char **argv) {
    uuid_t _uuid;

    log_beep_main = LOG_CATEGORY_GET("beepcomm");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    beep_flags_init(argc, argv);

    debug = comm_flags.debug;

    uloop_init();

    if(!comm_flags.ubus_stub) {
        ubus_ctx = beep_ubus_connect("beepcomm");
        if(!ubus_ctx) {
            LOG_ERROR(log_beep_main, "Failed to connect to ubus");
            return -1;
        }
    } else {
        LOG_INFO(log_beep_main, "UBUS STUB -- Not connecting to ubus");
    }

    uuid_generate(_uuid);
    uuid_unparse_lower(_uuid, uuid);
    LOG_INFO(log_beep_main, "Using uuid = %s", uuid);

    ubus_add_uloop(ubus_ctx);

    comm_control_object.name = "beep.comm.control";
    int ret = ubus_add_object(ubus_ctx, &comm_control_object);
    if(ret) {
        LOG_ERROR(log_beep_main, "Failed to add control object: %s",
                ubus_strerror(ret));
        return -1;
    } else {
        LOG_INFO(log_beep_main, "ubus control object added.");
    }

    // Ignore that stupid SIGPIPE.
    signal(SIGPIPE, SIG_IGN);

    if (comm_flags.autostart) {
        _start(NULL);
    }

    ubus_complete_request_allowed = false;

    uloop_run(); // Does not return

    fprintf(stderr, "\n\n...MAIN LOOP INTERRUPTED...\n\n");
    _stop(NULL);

    ubus_remove_object(ubus_ctx, &comm_control_object);
    beep_ubus_disconnect(ubus_ctx);
    uloop_done();
    free(log_beep_main);
    return 0;
}
