#define _GNU_SOURCE
#include <assert.h>
#include <inttypes.h>
#include <libubus.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "collectd.h"
#include "common.h"
#include "plugin.h"

// Don't know how long we are allowed to stay in a collectd read call but
// 2 seconds seems like a good timeout.
#define INVOKE_TIMEOUT                              (2000)

struct call_entry;

struct call_entry {
    char *path;
    char *method;
    char **result_str;
    int result_count;
    char type[DATA_MAX_NAME_LEN];
    struct call_entry *next;
};

struct call_result {
    uint64_t val;
    bool valid;
};

struct ubus_cb_priv {
    struct call_entry *entry;
    struct call_result *results;
};

struct ubus_context *ubus_ctx;
struct call_entry *call_head;

// Don't add path or method since ubusmon_config have to iterate each value
// in turn.
static struct call_entry *call_entry_new(void) {
    struct call_entry *entry = (struct call_entry *)
            malloc(sizeof(struct call_entry));
    if (!entry)
        return NULL;
    memset(entry, 0, sizeof(struct call_entry));
    return entry;
}

static void call_entry_free(struct call_entry *entry) {
    int i;

    if (entry) {
        if (entry->path)
            free(entry->path);
        if (entry->method)
            free(entry->method);
        if (entry->result_str) {
            for (i = 0; i < entry->result_count; i++) {
                if (entry->result_str[i])
                    free(entry->result_str[i]);
            }
            free(entry->result_str);
        }
        free(entry);
    }
}

static int call_entry_add_result(struct call_entry *entry,
        const char *result) {
    if (!entry || !result)
        return 1;

    entry->result_str = (char **)realloc(entry->result_str,
            sizeof(char *) * (entry->result_count + 1));
    if (!entry->result_str)
        return 1;

    entry->result_str[entry->result_count] = strdup(result);
    if (!entry->result_str[entry->result_count])
        return 1;
    entry->result_count++;

    return 0;
}

// Returns 0 for success.
static int call_entry_check(struct call_entry *entry) {
    int i;
    if (!entry || !entry->path || !entry->method || !entry->result_str
            || !entry->result_count)
        return 1;

    for (i = 0; i < entry->result_count; i++) {
        if (!entry->result_str[i])
            return 1;
    }
    return 0;
}

static int call_entry_create_type(struct call_entry *entry) {
    data_set_t data_set = {};
    int i;

    snprintf(data_set.type, DATA_MAX_NAME_LEN, "%s::%s",
            entry->path, entry->method);
    // Need to keep a copy with the call_entry so we don't have to keep
    // calling snprintf.
    snprintf(entry->type, DATA_MAX_NAME_LEN, "%s::%s",
            entry->path, entry->method);

    data_set.ds_num = entry->result_count;
    data_set.ds = calloc(entry->result_count, sizeof(data_source_t));

    if (!data_set.ds)
        return 1;

    for (i = 0; i < entry->result_count; i++) {
        data_source_t *ds = data_set.ds + i;
        strncpy(ds->name, entry->result_str[i], DATA_MAX_NAME_LEN - 1);
        ds->type = DS_TYPE_GAUGE;
        ds->min = 0;
        ds->max = NAN;
    }

    plugin_register_data_set(&data_set);

    free(data_set.ds);

    return 0;
}

static void call_list_add_entry(struct call_entry *entry) {
    if (entry) {
        entry->next = call_head;
        call_head = entry;
    }
}

static void call_list_free(void) {
    struct call_entry *p;
    while (call_head) {
        p = call_head;
        call_head = call_head->next;
        call_entry_free(p);
    }
}

static int ubusmon_config_call(oconfig_item_t *ci) {
    struct call_entry *entry;
    int i;

    entry = call_entry_new();
    if (!entry)
        return 1;

    for (i = 0; i < ci->children_num; i++) {
        oconfig_item_t *child = ci->children + i;

        if (!strcmp("path", child->key)) {
            cf_util_get_string(child, &entry->path);
        } else if (!strcmp("method", child->key)) {
            cf_util_get_string(child, &entry->method);
        } else if (!strcmp("result", child->key)) {
            char *key_string = NULL;
            cf_util_get_string(child, &key_string);
            if (key_string) {
                call_entry_add_result(entry, key_string);
                free(key_string);
            }
        } else {
            WARNING("ubusmon plugin: unknown key %s", child->key);
        }
    }

    if (call_entry_check(entry)) {
        WARNING("ubusmon plugin: incomplete call entry");
        call_entry_free(entry);
        return 1;
    }

    if (call_entry_create_type(entry)) {
        WARNING("ubusmon plugin: could not create type");
        call_entry_free(entry);
        return 1;
    }

    call_list_add_entry(entry);
    return 0;
}

static int ubusmon_config(oconfig_item_t *ci) {
    int i;

    for (i = 0; i < ci->children_num; i++) {
        oconfig_item_t *child = ci->children + i;

        if (!strcmp("call", child->key)) {
            ubusmon_config_call(child);
        } else {
            WARNING("ubusmon plugin: unknown config %s", child->key);
        }
    }

    return 0;
}

static int ubusmon_init(void) {
    if (!ubus_ctx)
        ubus_ctx = ubus_connect("/var/run/ubus.sock");
    return ubus_ctx ? 0 : 1;
}

static int ubusmon_shutdown(void) {
    if (ubus_ctx)
        ubus_free(ubus_ctx);
    call_list_free();
    return 0;
}

// Keep in sync with beep_ubus.
enum {
    BEEP_RESPONSE_SUCCESS,
    BEEP_RESPONSE_RESULT,
    BEEP_RESPONSE_ERROR_CODE,
    BEEP_RESPONSE_ERROR_MESSAGE,
    __BEEP_RESPONSE_MAX
};

static const struct blobmsg_policy beep_response_policy[] = {
    [BEEP_RESPONSE_SUCCESS] = { .name = "success", .type = BLOBMSG_TYPE_BOOL },
    [BEEP_RESPONSE_RESULT] = { .name = "result", .type = BLOBMSG_TYPE_UNSPEC },
    [BEEP_RESPONSE_ERROR_CODE] =
            { .name = "error_code", .type = BLOBMSG_TYPE_INT32 },
    [BEEP_RESPONSE_ERROR_MESSAGE] =
            { .name = "error_message", .type = BLOBMSG_TYPE_STRING },
};

static void ubus_cb(struct ubus_request *req, int type,
        struct blob_attr *msg) {
    struct ubus_cb_priv *priv = (struct ubus_cb_priv *)req->priv;
    struct blob_attr *tb[__BEEP_RESPONSE_MAX];
    struct blob_attr *attr;
    void *data;
    int len;
    int valid = 0;

    blobmsg_parse(beep_response_policy, ARRAY_SIZE(beep_response_policy),
        tb, blob_data(msg), blob_len(msg));

    if (!tb[BEEP_RESPONSE_SUCCESS]
            || !blobmsg_get_bool(tb[BEEP_RESPONSE_SUCCESS])
            || !tb[BEEP_RESPONSE_RESULT]) {
        return;
    }

    data = blobmsg_data(tb[BEEP_RESPONSE_RESULT]);
    len = blobmsg_data_len(tb[BEEP_RESPONSE_RESULT]);

    __blob_for_each_attr(attr, data, len) {
        struct blobmsg_hdr *hdr = blob_data(attr);
        int id = blob_id(attr);
        int i;
        uint16_t namelen = be16_to_cpu(hdr->namelen);
        bool check = blobmsg_check_attr(attr, true);

        if (!namelen || !check)
            continue;

        for (i = 0; i < priv->entry->result_count; i++) {
            if (priv->results[i].valid) {
                continue;
            }

            if (!strcmp(priv->entry->result_str[i], hdr->name)) {
                priv->results[i].valid = true;
                switch (id) {
                case BLOBMSG_TYPE_INT64:
                    priv->results[i].val = blobmsg_get_u64(attr);
                    break;
                case BLOBMSG_TYPE_INT32:
                    priv->results[i].val = (uint64_t)blobmsg_get_u32(attr);
                    break;
                case BLOBMSG_TYPE_INT16:
                    priv->results[i].val = (uint64_t)blobmsg_get_u16(attr);
                    break;
                case BLOBMSG_TYPE_INT8:
                //case BLOBMSG_TYPE_BOOL:
                    priv->results[i].val = (uint64_t)blobmsg_get_u8(attr);
                    break;
                default:
                    // Unsupported type, mark as not found.
                    priv->results[i].valid = false;
                    break;
                }

                if (priv->results[i].valid) {
                    // Check if we have read everything in entry.
                    valid++;
                    if (valid == priv->entry->result_count) {
                        return;
                    }
                }
            }
        }
    }
}

static void ubusmon_submit(struct call_entry *entry,
        struct call_result *results) {
    value_t *values;
    value_list_t vl = VALUE_LIST_INIT;
    int i;

    values = (value_t *)calloc(entry->result_count, sizeof(value_t));
    if (!values)
        return;

    for (i = 0; i < entry->result_count; i++) {
        // Need to send all results even if a valid response was
        // not found.
        values[i].gauge = results[i].val;
    }

    vl.values = values;
    vl.values_len = entry->result_count;

    sstrncpy(vl.host, hostname_g, sizeof(vl.host));
    sstrncpy(vl.plugin, "ubusmon", sizeof(vl.plugin));
    sstrncpy(vl.type, entry->type, sizeof(vl.type));

    plugin_dispatch_values(&vl);

    free(values);
}

static int ubusmon_read(void) {
    struct call_entry *entry;
    struct ubus_cb_priv priv;
    uint32_t id;
    int ret;

    if (!ubus_ctx)
        return 1;

    for(entry = call_head; entry; entry = entry->next) {
        if (ubus_lookup_id(ubus_ctx, entry->path, &id)) {
            continue;
        }

        priv.entry = entry;
        priv.results = (struct call_result *)calloc(entry->result_count,
                sizeof(struct call_result));
        if (!priv.results)
            return 1;

        ret = ubus_invoke(ubus_ctx, id, entry->method, NULL, ubus_cb, &priv,
                INVOKE_TIMEOUT);

        if (!ret) {
            ubusmon_submit(entry, priv.results);
        }

        free(priv.results);
    }

    return 0;
}

void module_register(void) {
    plugin_register_complex_config("ubusmon", ubusmon_config);
    plugin_register_init("ubusmon", ubusmon_init);
    plugin_register_read("ubusmon", ubusmon_read);
    plugin_register_shutdown("ubusmon", ubusmon_shutdown);
}
