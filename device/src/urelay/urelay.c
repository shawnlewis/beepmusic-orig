#include <signal.h>

#include "urelay.h"

#include "beep/flags.h"
#include "beep/beep_ubus.h"
#include "beep/urelay.h"
#include "beep/config.h"

bool debug = false;
struct blob_buf buf;
struct ubus_context *ctx = NULL;

int TCP_PROBE_COUNT;
int TCP_IDLE_TIME;
int TCP_INTERVAL;

extern int ubus_complete_request_allowed;

const struct blobmsg_policy response_blob_policy[] = {
    [BLOB_RESPONSE_ID] = {
        .name = "id",
        .type = BLOBMSG_TYPE_INT32
    },
    [BLOB_RESPONSE_MSG] = {
        .name = "msg",
        .type = BLOBMSG_TYPE_TABLE
    }
};

const struct blobmsg_policy error_blob_policy[] = {
    [BLOB_ERROR_ID] = {
        .name = "id",
        .type = BLOBMSG_TYPE_INT32
    },
    [BLOB_ERROR_CODE] = {
        .name = "code",
        .type = BLOBMSG_TYPE_INT32
    },
    [BLOB_ERROR_MSG] = {
        .name = "error_msg",
        .type = BLOBMSG_TYPE_STRING
    }
};

const struct blobmsg_policy invoke_blob_policy[] = {
    [BLOB_INVOKE_ID] = {
        .name = "id",
        .type = BLOBMSG_TYPE_INT32
    },
    [BLOB_INVOKE_PATH] = {
        .name = "path",
        .type = BLOBMSG_TYPE_STRING
    },
    [BLOB_INVOKE_METHOD] = {
        .name = "method",
        .type = BLOBMSG_TYPE_STRING
    },
    [BLOB_INVOKE_MSG] = {
        .name = "msg",
        .type = BLOBMSG_TYPE_TABLE
    }
};

const struct blobmsg_policy event_blob_policy[] = {
    [BLOB_EVENT_TYPE] = {
        .name = "type",
        .type = BLOBMSG_TYPE_STRING
    },
    [BLOB_EVENT_MSG] = {
        .name = "msg",
        .type = BLOBMSG_TYPE_TABLE
    }
};

struct flag_vals {
     int urelay_port;
     bool debug;
};

struct flag_vals urelay_flags = {
    .urelay_port = URELAY_PORT,
    .debug = false
};

static const BeepFlag flags[] = {
    BEEP_FLAG("urelay_port", BEEP_FLAG_INT, &urelay_flags.urelay_port, NULL, NULL),
    BEEP_FLAG("debug", BEEP_FLAG_BOOL, &urelay_flags.debug, NULL, NULL)
};

BEEP_INIT_FLAGS(flags)

/*
 * Main application code -- gets us into the uloop loop and never exits
 */

static void add_obj_cb(struct ubus_context *ctx,
        struct ubus_object *obj, void *priv) {
    LOG_INFO(log_beep_main, "ubus object added.");
}

static void relay_main(void)
{
    int ret;

    ret = init_relay_server();
    if(ret) {
        LOG_ERROR(log_beep_main, "Failed to bind to port %d\n", server_port);
        return;
    }

    ret = ubus_add_object_async(ctx, &relay_object, &add_obj_cb, NULL);
    if (ret) {
        LOG_ERROR(log_beep_main, "Failed to add object: %s\n",
                ubus_strerror(ret));
        return;
    }

    uloop_run();
}

int main(int argc, char **argv)
{
    log_beep_main = LOG_CATEGORY_GET("urelay");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    beep_flags_init(argc, argv);

    ubus_complete_request_allowed = false;

    TCP_PROBE_COUNT = beep_config_devel_get_int("urelay_ka_probe_count", 1);
    TCP_INTERVAL = beep_config_devel_get_int("urelay_ka_interval", 10);
    TCP_IDLE_TIME = beep_config_devel_get_int("urelay_ka_idle_time", 20);

    debug = urelay_flags.debug;
    server_port = urelay_flags.urelay_port;

    char buf[16] = "urelay";
    relay_object.name = buf;
    relay_type.name = buf;

    uloop_init();

    ctx = beep_ubus_connect("urelay");
    if(!ctx) {
        LOG_ERROR(log_beep_main, "Failed to connect to ubus");
        return -1;
    }

    ubus_add_uloop(ctx);

    // Ignore that stupid SIGPIPE.
    signal(SIGPIPE, SIG_IGN);

    relay_main();

    ubus_free(ctx);
    uloop_done();
    return 0;
}
