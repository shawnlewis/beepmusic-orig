#define _GNU_SOURCE
#include <malloc.h>
#include <stdio.h>

#include "beep_ubus.h"
#include "beep_ubus_debug.h"

static struct ubus_object debug_ubus_object;
static struct ubus_object_type debug_ubus_type;
static struct ubus_method *debug_methods;
static char *debug_name;

static int ubus_mallinfo(struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    struct blob_buf *args = calloc(1, sizeof(struct blob_buf));
    struct mallinfo mi;

    if (!args)
        return 1;

    blob_buf_init(args, 0);
    mi = mallinfo();

    blobmsg_add_u32(args, "arena", mi.arena);
    blobmsg_add_u32(args, "ordblks", mi.ordblks);
    blobmsg_add_u32(args, "smblks", mi.smblks);
    blobmsg_add_u32(args, "hblks", mi.hblks);
    blobmsg_add_u32(args, "hblkhd", mi.hblkhd);
    blobmsg_add_u32(args, "usmblks", mi.usmblks);
    blobmsg_add_u32(args, "fsmblks", mi.fsmblks);
    blobmsg_add_u32(args, "uordblks", mi.uordblks);
    blobmsg_add_u32(args, "fordblks", mi.fordblks);
    blobmsg_add_u32(args, "keepcost", mi.keepcost);

    beep_reply_success(ctx, req, args->head);

    blob_buf_free(args);
    free(args);

    return 0;
}

static const struct ubus_method default_debug_methods[] = {
    UBUS_METHOD_NOARG("mallinfo", ubus_mallinfo)
};

static void beep_ubus_cleanup(void) {
    memset(&debug_ubus_object, 0, sizeof(struct ubus_object));
    memset(&debug_ubus_type, 0, sizeof(struct ubus_object_type));
    if (debug_name) {
        free(debug_name);
        debug_name = NULL;
    }
    if (debug_methods) {
        free(debug_methods);
        debug_methods = NULL;
    }
}

int beep_ubus_debug_start(const struct ubus_method *methods,
        size_t n_methods, const char* name) {
    struct ubus_context *ctx = beep_ubus_get_ctx();
    size_t n_debug_methods = sizeof(default_debug_methods)
            / sizeof(struct ubus_method);

    if (!ctx || !name || !strlen(name) || debug_methods)
        return 1;

    if ((!methods && n_methods) || (methods && !n_methods))
        return 1;

    debug_methods = (struct ubus_method *)malloc(sizeof(struct ubus_method)
            * (n_methods + n_debug_methods));

    if (!debug_methods)
        return 1;

    memcpy(debug_methods, default_debug_methods,
            sizeof(struct ubus_method) * n_debug_methods);
    if (n_methods) {
        memcpy(debug_methods + n_debug_methods, methods,
                sizeof(struct ubus_method) * n_methods);
        n_debug_methods += n_methods;
    }

    if (asprintf(&debug_name, "beep.debug.%s", name) == -1) {
        beep_ubus_cleanup();
        return 1;
    }

    debug_ubus_type.name = debug_name;
    debug_ubus_type.id = 0;
    debug_ubus_type.methods = debug_methods;
    debug_ubus_type.n_methods = n_debug_methods;

    debug_ubus_object.name = debug_name;
    debug_ubus_object.type = &debug_ubus_type;
    debug_ubus_object.methods = debug_methods;
    debug_ubus_object.n_methods = n_debug_methods;

    if (ubus_add_object_async(ctx, &debug_ubus_object, NULL, NULL)) {
        beep_ubus_cleanup();
    }

    return 0;
}

void beep_ubus_debug_stop(void) {
    if (debug_name) {
        ubus_remove_object(beep_ubus_get_ctx(), &debug_ubus_object);
        beep_ubus_cleanup();
    }
}
