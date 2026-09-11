#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>

#include "debug.h"
#include "beep_ubus.h"
#include "beep_uloop.h"
#include "beep_data.h"

// #define TRACE_BEEP_DATA

struct operation {
    char *key;
    void *value;
    size_t len;
    bool global;

    bool ok;

    union {
        beep_data_get_callback_t get;
        beep_data_set_callback_t set;
        beep_data_delete_callback_t delete;
    } callback;

    void *userdata;
    struct blob_attr *response;
};

/*
 * Get
 */

static void data_get_done(void *userdata) {
#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "start");
#endif
    struct operation *op = (struct operation *)userdata;

    if(op->callback.get) {
        (*op->callback.get)(op->key,
                op->value,
                op->len,
                op->userdata);
    }

    free(op->key);
    free(op->response);
    free(op);

#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "end");
#endif
}

enum {
    GET_KEY,
    GET_VALUE,
    __GET_MAX
};

static const struct blobmsg_policy cloud_response_get_policy[] = {
    [GET_KEY] = { .name = "key", .type = BLOBMSG_TYPE_STRING },
    [GET_VALUE] = { .name = "value", .type = BLOBMSG_TYPE_STRING },
};

static void data_get_task(void *userdata) {
#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "start");
#endif

    struct operation *op = (struct operation *)userdata;
    struct blob_buf *args = calloc(1, sizeof(struct blob_buf));
    blob_buf_init(args, 0);

    blobmsg_add_string(args, "key", op->key);
    blobmsg_add_u8(args, "global", (uint8_t)op->global);

    int ret = beep_ubus_invoke(
            "beep.cloud", "get", args->head, &op->response);

    blob_buf_free(args);
    free(args);

    if(ret) {
        LOG_ERROR(log_beep_main, "ubus error: %s", ubus_strerror(ret));
        goto out_error;
    }

    struct blob_attr *parsed[__BEEP_RESPONSE_MAX];
    if(!beep_parse_response(op->response, parsed)) {
        LOG_ERROR(log_beep_main, "beep_parse_response failed");
        goto out_error;
    }

    if(!blobmsg_get_bool(parsed[BEEP_RESPONSE_SUCCESS])) {
        LOG_ERROR(log_beep_main, "Got beep error. code: %d msg: %s",
                blobmsg_get_u32(parsed[BEEP_RESPONSE_ERROR_CODE]),
                blobmsg_get_string(parsed[BEEP_RESPONSE_ERROR_MESSAGE]));
        goto out_error;
    }

    struct blob_attr *tb_result[__GET_MAX];
    if(blobmsg_parse(cloud_response_get_policy,
                __GET_MAX,
                tb_result,
                blobmsg_data(parsed[BEEP_RESPONSE_RESULT]),
                blobmsg_data_len(parsed[BEEP_RESPONSE_RESULT]))) {
        LOG_ERROR(log_beep_main, "Failed to parse result table");
        goto out_error;
    }

    if(!tb_result[GET_KEY] || !tb_result[GET_VALUE]) {
        // This is bad but not a fatal condition.  Sometimes this happens
        // when beepcloud fails to communicate with backend
        LOG_WARN(log_beep_main, "Invalid result");
        goto out_error;
    } else {
        op->value = blobmsg_data(tb_result[GET_VALUE]);
        op->len = blobmsg_data_len(tb_result[GET_VALUE]) - 1;
    }

#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "end");
#endif
    return;

out_error:
    op->value = NULL;
    op->len = 0;
}

void beep_data_get(const char *key,
        bool global, beep_data_get_callback_t done, void *userdata) {
#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "start");
#endif

    // Make our own internal copies to work with
    struct operation *op = calloc(1, sizeof(struct operation));
    op->key = strdup(key);
    op->global = global;
    op->callback.get = done;
    op->userdata = userdata;

    beep_async_task(&data_get_task, &data_get_done, op);

#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "end");
#endif
}


/*
 * Set
 */

static void data_set_done(void *userdata) {
#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "start");
#endif
    struct operation *op = (struct operation *)userdata;

    if(op->callback.set) {
        (*op->callback.set)(op->key,
                op->ok,
                op->userdata);
    }

    free(op->key);
    free(op->value);
    free(op->response);
    free(op);

#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "end");
#endif
}

enum {
    SET_KEY,
    SET_OK,
    __SET_MAX
};

static const struct blobmsg_policy cloud_response_set_policy[] = {
    [SET_KEY] = { .name = "key", .type = BLOBMSG_TYPE_STRING },
    [SET_OK] = { .name = "ok", .type = BLOBMSG_TYPE_BOOL },
};

static void data_set_task(void *userdata) {
#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "start");
#endif

    struct operation *op = (struct operation *)userdata;
    struct blob_buf *args = calloc(1, sizeof(struct blob_buf));
    blob_buf_init(args, 0);

    blobmsg_add_string(args, "key", op->key);

    char *value_terminated = malloc(op->len + 1);
    memcpy(value_terminated, op->value, op->len);
    value_terminated[op->len] = 0;
    if(blobmsg_add_field(args, BLOBMSG_TYPE_STRING, "value",
            value_terminated, op->len + 1) == -1) {
        LOG_ERROR(log_beep_main, "Error adding value field");
        // This is an OOM condition, should abort
        abort();
    }
    free(value_terminated);

    blobmsg_add_u8(args, "global", (uint8_t)op->global);

    int ret = beep_ubus_invoke(
            "beep.cloud", "set", args->head, &op->response);

    blob_buf_free(args);
    free(args);

    if(ret) {
        LOG_ERROR(log_beep_main, "ubus error: %s", ubus_strerror(ret));
        goto out_error;
    }

    struct blob_attr *parsed[__BEEP_RESPONSE_MAX];
    if(!beep_parse_response(op->response, parsed)) {
        LOG_ERROR(log_beep_main, "beep_parse_response failed");
        goto out_error;
    }

    if(!blobmsg_get_bool(parsed[BEEP_RESPONSE_SUCCESS])) {
        LOG_ERROR(log_beep_main, "Got beep error. code: %d msg: %s",
                blobmsg_get_u32(parsed[BEEP_RESPONSE_ERROR_CODE]),
                blobmsg_get_string(parsed[BEEP_RESPONSE_ERROR_MESSAGE]));
        goto out_error;
    }

    struct blob_attr *tb_result[__SET_MAX];
    if(blobmsg_parse(cloud_response_set_policy,
                __SET_MAX,
                tb_result,
                blobmsg_data(parsed[BEEP_RESPONSE_RESULT]),
                blobmsg_data_len(parsed[BEEP_RESPONSE_RESULT]))) {
        LOG_ERROR(log_beep_main, "Failed to parse result table");
        goto out_error;
    }

    if(!tb_result[SET_KEY] || !tb_result[SET_OK]) {
        LOG_ERROR(log_beep_main, "Invalid result");
        goto out_error;
    }

    op->ok = blobmsg_get_bool(tb_result[SET_OK]);

#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "end");
#endif
    return;

out_error:
    op->ok = false;
}

void beep_data_set(const char *key, const void *value, size_t len,
        bool global, beep_data_set_callback_t done, void *userdata) {
#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "start");
#endif

    // Make our own internal copies to work with
    struct operation *op = calloc(1, sizeof(struct operation));
    op->key = strdup(key);
    op->value = malloc(len);
    memcpy(op->value, value, len);
    op->len = len;
    op->global = global;
    op->callback.set = done;
    op->userdata = userdata;

    beep_async_task(&data_set_task, &data_set_done, op);

#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "end");
#endif
}

/*
 * Delete
 */

static void data_delete_done(void *userdata) {
#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "start");
#endif
    struct operation *op = (struct operation *)userdata;

    if(op->callback.delete) {
        (*op->callback.delete)(op->key,
                op->ok,
                op->userdata);
    }

    free(op->key);
    free(op->response);
    free(op);

#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "end");
#endif
}

enum {
    DELETE_KEY,
    DELETE_OK,
    __DELETE_MAX
};

static const struct blobmsg_policy cloud_response_delete_policy[] = {
    [DELETE_KEY] = { .name = "key", .type = BLOBMSG_TYPE_STRING },
    [DELETE_OK] = { .name = "ok", .type = BLOBMSG_TYPE_BOOL },
};

static void data_delete_task(void *userdata) {
#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "start");
#endif

    struct operation *op = (struct operation *)userdata;
    struct blob_buf *args = calloc(1, sizeof(struct blob_buf));
    blob_buf_init(args, 0);

    blobmsg_add_string(args, "key", op->key);
    blobmsg_add_u8(args, "global", (uint8_t)op->global);

    int ret = beep_ubus_invoke(
            "beep.cloud", "delete", args->head, &op->response);

    blob_buf_free(args);
    free(args);

    if(ret) {
        LOG_ERROR(log_beep_main, "ubus error: %s", ubus_strerror(ret));
    }

    struct blob_attr *parsed[__BEEP_RESPONSE_MAX];
    if(!beep_parse_response(op->response, parsed)) {
        LOG_ERROR(log_beep_main, "beep_parse_response failed");
        goto out_error;
    }

    if(!blobmsg_get_bool(parsed[BEEP_RESPONSE_SUCCESS])) {
        LOG_ERROR(log_beep_main, "Got beep error. code: %d msg: %s",
                blobmsg_get_u32(parsed[BEEP_RESPONSE_ERROR_CODE]),
                blobmsg_get_string(parsed[BEEP_RESPONSE_ERROR_MESSAGE]));
        goto out_error;
    }

    struct blob_attr *tb_result[__DELETE_MAX];
    if(blobmsg_parse(cloud_response_set_policy,
                __DELETE_MAX,
                tb_result,
                blobmsg_data(parsed[BEEP_RESPONSE_RESULT]),
                blobmsg_data_len(parsed[BEEP_RESPONSE_RESULT]))) {
        LOG_ERROR(log_beep_main, "Failed to parse result table");
        goto out_error;
    }

    if(!tb_result[DELETE_KEY] || !tb_result[DELETE_OK]) {
        LOG_ERROR(log_beep_main, "Invalid result");
        goto out_error;
    }

    op->ok = blobmsg_get_bool(tb_result[DELETE_OK]);

#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "end");
#endif
    return;

out_error:
    op->ok = false;
}

void beep_data_delete(const char *key, bool global,
        beep_data_set_callback_t done, void *userdata) {
#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "start");
#endif

    // Make our own internal copies to work with
    struct operation *op = calloc(1, sizeof(struct operation));
    op->key = strdup(key);
    op->global = global;
    op->callback.delete = done;
    op->userdata = userdata;

    beep_async_task(&data_delete_task, &data_delete_done, op);

#ifdef TRACE_BEEP_DATA
    LOG_DEBUG(log_beep_main, "end");
#endif
}

