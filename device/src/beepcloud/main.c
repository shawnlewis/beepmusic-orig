#include <signal.h>
#include <pthread.h>
#include <openssl/crypto.h>

#include "beepcloud.h"

#include "beep/flags.h"
#include "beep/beep_ubus.h"
#include "beep/config.h"

char url_prefix[URL_PREFIX_MAX_LEN];
char group_id[GROUP_ID_MAX_LEN];
char cluster_id[CLUSTER_ID_MAX_LEN];
char *esc_cluster_id;

struct ubus_context *ctx;
CURLSH *curl_share;

typedef enum {
    OP_UNKNOWN,
    OP_GET,
    OP_SET,
    OP_DELETE,
} cloud_op;

enum {
    GET_KEY,
    GET_GLOBAL,
    __GET_MAX
};

static const struct blobmsg_policy get_policy[] = {
    [GET_KEY] = { .name = "key", .type = BLOBMSG_TYPE_STRING },
    [GET_GLOBAL] = { .name = "global", .type = BLOBMSG_TYPE_BOOL },
};

enum {
    SET_KEY,
    SET_VALUE,
    SET_GLOBAL,
    __SET_MAX
};

static const struct blobmsg_policy set_policy[] = {
    [SET_KEY] = { .name = "key", .type = BLOBMSG_TYPE_STRING },
    [SET_VALUE] = { .name = "value", .type = BLOBMSG_TYPE_STRING },
    [SET_GLOBAL] = { .name = "global", .type = BLOBMSG_TYPE_BOOL },
};

enum {
    DELETE_KEY,
    DELETE_GLOBAL,
    __DELETE_MAX
};

static const struct blobmsg_policy delete_policy[] = {
    [DELETE_KEY] = { .name = "key", .type = BLOBMSG_TYPE_STRING },
    [DELETE_GLOBAL] = { .name = "global", .type = BLOBMSG_TYPE_BOOL },
};

// Required OpenSSL and libcurl locking code

static pthread_mutex_t curl_lock;
static pthread_mutex_t *lockarray;

static void lock_callback(int mode, int type, const char *file, int line) {
  (void)file;
  (void)line;
  if (mode & CRYPTO_LOCK) {
    pthread_mutex_lock(&(lockarray[type]));
  } else {
    pthread_mutex_unlock(&(lockarray[type]));
  }
}

void thread_id(CRYPTO_THREADID *id) {
  unsigned long ret;

  ret = (unsigned long)pthread_self();
  CRYPTO_THREADID_set_numeric(id, ret);
}

static void init_locks(void) {
  int i;

  // Openssl
  lockarray = (pthread_mutex_t *)OPENSSL_malloc(CRYPTO_num_locks() *
                                            sizeof(pthread_mutex_t));
  for (i=0; i<CRYPTO_num_locks(); i++) {
    pthread_mutex_init(&(lockarray[i]), NULL);
  }

  CRYPTO_THREADID_set_callback(thread_id);
  CRYPTO_set_locking_callback(lock_callback);

  // libcurl
  pthread_mutex_init(&curl_lock, NULL);
}

static void kill_locks(void) {
  int i;

  // Openssl
  CRYPTO_set_locking_callback(NULL);
  for (i=0; i<CRYPTO_num_locks(); i++)
    pthread_mutex_destroy(&(lockarray[i]));

  OPENSSL_free(lockarray);

  // libcurl
  pthread_mutex_destroy(&curl_lock);
}

static void lock_curl(CURL *handle, curl_lock_data data, curl_lock_access
        access, void *useptr) {
    (void)handle;
    (void)data;
    (void)access;
    (void)useptr;

    pthread_mutex_lock(&curl_lock);
}

static void unlock_curl(CURL *handle, curl_lock_data data, void *useptr) {
    (void)handle;
    (void)data;
    (void)useptr;

    pthread_mutex_unlock(&curl_lock);
}

// End locking code

static inline cloud_op get_op(const char *method) {
    if(!strcasecmp(method,"get")) {
        return OP_GET;
    } else if(!strcasecmp(method,"set")) {
        return OP_SET;
    } else if(!strcasecmp(method,"delete")) {
        return OP_DELETE;
    } else {
        LOG_WARN(log_beep_main, "Programming error: unknown op %s", method);
        return OP_UNKNOWN;
    }
}

static const char *get_raw_key(const char *key, const char *namespace, bool global) {
    static char raw_key[RAW_KEY_MAX_LEN]; // TODO: Arbitrary size
    snprintf(raw_key, RAW_KEY_MAX_LEN, "%s.%s.%s",
            global ? "global" : group_id,
            namespace ? namespace : "default",
            key);
    return raw_key;
}

struct pending_op {
    char key[KEY_MAX_LEN]; // TODO: Arbitrary size
    struct ubus_request_data def_req;
};

static void get_done(const char *key, const char *value, size_t len,
        void *userdata) {
    struct pending_op *po = (struct pending_op *)userdata;
    struct blob_buf buf;
    char *terminated_value;

    // LOG_DEBUG(log_beep_main, "Received '%s', len = %zu",
    //         (char *)value, len); // DEBUG

    memset(&buf, 0, sizeof(struct blob_buf));
    blob_buf_init(&buf, 0);

    blobmsg_add_string(&buf, "key", po->key);

    if(len) {
        terminated_value = malloc(len + 1);
        memcpy(terminated_value, value, len);
        terminated_value[len] = 0;
        blobmsg_add_field(&buf, BLOBMSG_TYPE_STRING, "value", value, len + 1);
        // TODO: Check for failures
        free(terminated_value);
    } else {
        blobmsg_add_string(&buf, "value", "");
    }

    beep_reply_success(ctx, &po->def_req, buf.head);
    blob_buf_free(&buf);
    ubus_complete_deferred_request(ctx, &po->def_req, 0);
    free(po);
}

static void set_done(const char *key, bool ok, void *userdata) {
    struct pending_op *po = (struct pending_op *)userdata;
    struct blob_buf buf;

    memset(&buf, 0, sizeof(struct blob_buf));
    blob_buf_init(&buf, 0);

    blobmsg_add_string(&buf, "key", po->key);
    blobmsg_add_u8(&buf, "ok", (uint8_t)ok);

    beep_reply_success(ctx, &po->def_req, buf.head);
    blob_buf_free(&buf);
    ubus_complete_deferred_request(ctx, &po->def_req, 0);
    free(po);
}

static void delete_done(const char *key, bool ok, void *userdata) {
    struct pending_op *po = (struct pending_op *)userdata;
    struct blob_buf buf;

    memset(&buf, 0, sizeof(struct blob_buf));
    blob_buf_init(&buf, 0);

    blobmsg_add_string(&buf, "key", po->key);
    blobmsg_add_u8(&buf, "ok", (uint8_t)ok);

    beep_reply_success(ctx, &po->def_req, buf.head);
    blob_buf_free(&buf);
    ubus_complete_deferred_request(ctx, &po->def_req, 0);
    free(po);
}

static int operate(struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    struct blob_attr *tb[5] = {0}; // TODO: Arbitrarily large tb array
    char *key;
    char *value;
    size_t len;
    struct pending_op *po;
    bool global;

    cloud_op op = get_op(method);

    if(op == OP_GET) {
        blobmsg_parse(get_policy, __GET_MAX, tb, blob_data(msg), blob_len(msg));
        if(!tb[GET_KEY]) {
            LOG_ERROR(log_beep_main, "Missing required argument \"key\"");
            beep_reply_error(ctx, req, "Missing required argument \"key\"",
                    BEEP_UBUS_ERROR);
            return 0;
        } else if(!tb[GET_GLOBAL]) {
            LOG_ERROR(log_beep_main, "Missing required argument \"global\"");
            beep_reply_error(ctx, req, "Missing required argument \"global\"",
                    BEEP_UBUS_ERROR);
            return 0;
        }

        key = blobmsg_get_string(tb[GET_KEY]);
        global = blobmsg_get_bool(tb[GET_GLOBAL]);

        po = calloc(1, sizeof(struct pending_op));
        ubus_defer_request(ctx, req, &po->def_req);
        strncpy(po->key, key, KEY_MAX_LEN - 1);
        data_get(get_raw_key(key, NULL, global), get_done, po);
    } else if(op == OP_SET) {
        blobmsg_parse(set_policy, __SET_MAX, tb, blob_data(msg), blob_len(msg));
        if(!tb[SET_KEY]) {
            LOG_ERROR(log_beep_main, "Missing required argument \"key\"");
            beep_reply_error(ctx, req, "Missing required argument \"key\"",
                    BEEP_UBUS_ERROR);
            return 0;
        } else if(!tb[SET_VALUE]) {
            LOG_ERROR(log_beep_main, "Missing required argument \"value\"");
            beep_reply_error(ctx, req, "Missing required argument \"value\"",
                    BEEP_UBUS_ERROR);
            return 0;
        } else if(!tb[SET_GLOBAL]) {
            LOG_ERROR(log_beep_main, "Missing required argument \"global\"");
            beep_reply_error(ctx, req, "Missing required argument \"global\"",
                    BEEP_UBUS_ERROR);
            return 0;
        }

        key = blobmsg_get_string(tb[SET_KEY]);
        value = blobmsg_data(tb[SET_VALUE]);
        len = blobmsg_data_len(tb[SET_VALUE]) - 1;
        global = blobmsg_get_bool(tb[SET_GLOBAL]);

        // This is potentially private user info, do not log in production
        // LOG_INFO(log_beep_main, "value = '%s', len = %zu", value, len);

        po = calloc(1, sizeof(struct pending_op));
        ubus_defer_request(ctx, req, &po->def_req);
        strncpy(po->key, key, KEY_MAX_LEN - 1);
        data_set(get_raw_key(key, NULL, global),
                value, len, set_done, po);
    } if(op == OP_DELETE) {
        blobmsg_parse(get_policy, __DELETE_MAX, tb, blob_data(msg), blob_len(msg));
        if(!tb[DELETE_KEY]) {
            LOG_ERROR(log_beep_main, "Missing required argument \"key\"");
            beep_reply_error(ctx, req, "Missing required argument \"key\"",
                    BEEP_UBUS_ERROR);
            return 0;
        } else if(!tb[DELETE_GLOBAL]) {
            LOG_ERROR(log_beep_main, "Missing required argument \"global\"");
            beep_reply_error(ctx, req, "Missing required argument \"global\"",
                    BEEP_UBUS_ERROR);
            return 0;
        }

        key = blobmsg_get_string(tb[GET_KEY]);
        global = blobmsg_get_bool(tb[GET_GLOBAL]);

        po = calloc(1, sizeof(struct pending_op));
        ubus_defer_request(ctx, req, &po->def_req);
        strncpy(po->key, key, KEY_MAX_LEN - 1);
        data_delete(get_raw_key(key, NULL, global), delete_done, po);
    }

    return 0;
}

static const struct ubus_method cloud_methods[] = {
    UBUS_METHOD("get", operate, get_policy),
    UBUS_METHOD("set", operate, set_policy),
    UBUS_METHOD("delete", operate, delete_policy),
};

static struct ubus_object_type cloud_type =
        UBUS_OBJECT_TYPE("beep.cloud", cloud_methods);

static struct ubus_object cloud_object = {
    .name = "beep.cloud",
    .type = &cloud_type,
    .methods = cloud_methods,
    .n_methods = ARRAY_SIZE(cloud_methods)
};

static void subscribe_cb(const char *component, struct blob_attr *state,
        struct blob_attr *event_data, const char *event_type) {
    struct blob_attr *tb_state[__MANAGER_STATE_MAX];
    struct blob_attr *tb_local[__MANAGER_LOCAL_MAX];

    if(blobmsg_parse(manager_state_policy, __MANAGER_STATE_MAX,
                tb_state, blobmsg_data(state), blobmsg_data_len(state))) {
        LOG_ERROR(log_beep_main, "Failed to parse manager state");
        abort();
    }

    if(!tb_state[MANAGER_STATE_LOCAL_DEVICE]) {
        LOG_ERROR(log_beep_main, "Invalid manager state (no local device table)");
        abort();
    }

    if(blobmsg_parse(manager_local_policy, __MANAGER_LOCAL_MAX,
                tb_local, blobmsg_data(tb_state[MANAGER_STATE_LOCAL_DEVICE]),
                blobmsg_data_len(tb_state[MANAGER_STATE_LOCAL_DEVICE]))) {
        LOG_ERROR(log_beep_main, "Error parsing local device table");
        abort();
    }

    if(!tb_local[MANAGER_LOCAL_SOURCE_ID]) {
        LOG_ERROR(log_beep_main, "Local device table has no field SOURCE_ID");
        abort();
    }

    snprintf(group_id, GROUP_ID_MAX_LEN, "group.%s",
            blobmsg_get_string(tb_local[MANAGER_LOCAL_SOURCE_ID]));

    LOG_INFO(log_beep_main, "Group_id = %s", group_id);
}

static void init_curl(void) {
    char host[HOST_MAX_LEN];
    char port[PORT_MAX_LEN];
    char path[PATH_MAX_LEN];

    int ret = beep_config_data_read("cluster_id", cluster_id,
            CLUSTER_ID_MAX_LEN);
    if (ret == -1) {
        strncpy(cluster_id, DEFAULT_CLUSTER_ID, CLUSTER_ID_MAX_LEN);
    }

    // Create an escaped copy of cluster_id
    {
        CURL *curl = curl_easy_init();
        if(curl) {
            esc_cluster_id = curl_easy_escape(curl, cluster_id, 0);
            if(!esc_cluster_id) {
                LOG_ERROR(log_beep_main, "Failed to escape cluster id");
                abort();
            }
            curl_easy_cleanup(curl);
        } else {
            LOG_ERROR(log_beep_main, "Failed to create easy curl handle "
                    "for cluster_id escape");
            abort();
        }
    }

    ret = beep_config_static_read("data_host", host, HOST_MAX_LEN);
    if (ret == -1) {
        strncpy(host, DEFAULT_HOST, HOST_MAX_LEN);
    }

    ret = beep_config_static_read("data_port", port, PORT_MAX_LEN);
    if (ret == -1) {
        strncpy(port, DEFAULT_PORT, PORT_MAX_LEN);
    }

    ret = beep_config_static_read("data_path", path, PATH_MAX_LEN);
    if (ret == -1) {
        strncpy(path, DEFAULT_PATH, PATH_MAX_LEN);
    }

    snprintf(url_prefix, URL_PREFIX_MAX_LEN, "https://%s:%s%s", host, port, path);
    LOG_INFO(log_beep_main, "Using backend url %s", url_prefix);
    LOG_INFO(log_beep_main, "Cluster_id = %s", cluster_id);

    curl_global_init(CURL_GLOBAL_ALL);
    curl_share = curl_share_init();
    if(curl_share) {
        curl_share_setopt(curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        curl_share_setopt(curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
        curl_share_setopt(curl_share, CURLSHOPT_LOCKFUNC, lock_curl);
        curl_share_setopt(curl_share, CURLSHOPT_UNLOCKFUNC, unlock_curl);
    }
}

static void main_loop(void) {
    int ret;

    ret = ubus_add_object(ctx, &cloud_object);
    if(ret) {
        LOG_ERROR(log_beep_main, "Failed to add object: %s\n",
                ubus_strerror(ret));
        return;
    }

    uloop_run();
}

void cleanup(void) {
    if(curl_share) {
        curl_share_cleanup(curl_share);
        curl_share = NULL;
    }

    if(esc_cluster_id) {
        curl_free(esc_cluster_id);
        esc_cluster_id = NULL;
    }

    curl_global_cleanup();
}

int main(int argc, char *argv[]) {
    log_beep_main = LOG_CATEGORY_GET("beepcloud");
    LOG_CATEGORY_SET_PRIORITY(log_beep_main, LOG_PRIORITY_DEBUG);

    init_locks();

    beep_flags_init(argc, argv);

    uloop_init();

    ctx = beep_ubus_connect("beepcloud");
    if(!ctx) {
        LOG_ERROR(log_beep_main, "Failed to connect to ubus");
        return -1;
    }

    ubus_add_uloop(ctx);

    signal(SIGPIPE, SIG_IGN);

    beep_ubus_subscribe("manager", subscribe_cb, NULL);

    init_curl();
    main_loop();
    cleanup();

    ubus_free(ctx);
    uloop_done();

    kill_locks();
    return 0;
}
