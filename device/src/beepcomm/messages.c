#define _GNU_SOURCE

#include "beepcomm.h"

#include <sys/eventfd.h>

/*
 * app functions
 */

static const char *extract_app_name(const char *uri) {
    static char app_name[64] = {0,};
    if(strstr(uri, "/castchat/") != uri) {
        return NULL;
    }

    strncpy(app_name, uri + 10, 63);
    return app_name;
}

/*
 * client, client comm, client list management
 */

enum {
    /*
     * Brand new client, need to get session id for app target.
     */
    CL_STATE_NEW = 0,

    /*
     * Session ID known and valid, need to start hello timeout.
     */
    CL_STATE_CONNECTED,

    /*
     * Hello timeout started, waiting for hello.
     */
    CL_STATE_WAIT_BIND,

    /*
     * Hello received and client bound, need to start ping/pong timers.
     */
    CL_STATE_BOUND,

    /*
     * Ping/pong timers started.
     */
    CL_STATE_ACTIVE,

    /*
     * Client has been marked for finalizing, need to destroy.
     */
    CL_STATE_FINALIZING,

    /*
     * Destroy client has been initiated, client is removed from
     * client list and will be destroyed.
     */
    CL_STATE_DESTROY_PENDING
};

// Each client struct represents one connection between a client and an app
struct client {
    struct messages_context *messages_ctx;
    struct mg_connection *conn;
    struct uloop_timeout t_hello;
    struct uloop_timeout t_ping;
    struct uloop_timeout t_pong;
    pthread_mutex_t lock;

    struct list_head head;

    char uuid[40];
    char user_agent[64];
    char *app_name;
    int state;
    int session_id;
    long hash;
    bool pong;
};

static struct messages_context *_messages_ctx = NULL;  // Necessary evil

static void finalize_client(struct client *cl);

#include "beep/beeplib.h"

static void lock_client_list(struct messages_context *ctx) {
    /*
    fprintf(stderr, "LOCKING CLIENT LIST: %p %p\n", ctx, &ctx->client_list_lock);
    print_trace();
    */
    int ret;
    if((ret = pthread_mutex_lock(&ctx->client_list_lock))) {
        LOG_ERROR(log_beep_main, "Unable to lock client list: %s",
                strerror(ret));
        abort();
    }
    /*
    fprintf(stderr, "CLIENT LOCKED LIST: %p %p\n", ctx, &ctx->client_list_lock);
    */
}

static void unlock_client_list(struct messages_context *ctx) {
    /*
    fprintf(stderr, "UNLOCKING CLIENT LIST: %p %p\n", ctx, &ctx->client_list_lock);
    print_trace();
    */
    int ret;
    if((ret = pthread_mutex_unlock(&ctx->client_list_lock))) {
        LOG_ERROR(log_beep_main, "Unable to unlock client list: %s",
                strerror(ret));
        abort();
    }
    /*
    fprintf(stderr, "CLIENT UNLOCKED LIST: %p %p\n", ctx, &ctx->client_list_lock);
    */
}

static void lock_client(struct client *cl) {
    /*
    fprintf(stderr, "LOCKING CLIENT: %p %p\n", cl, &cl->lock);
    print_trace();
    */
    int ret;
    if((ret = pthread_mutex_lock(&cl->lock))) {
        LOG_ERROR(log_beep_main, "Unable to lock client conn: %s",
                strerror(ret));
        abort();
    }
    /*
    fprintf(stderr, "CLIENT LOCKED: %p %p\n", cl, &cl->lock);
    */
}

static void unlock_client(struct client *cl) {
    /*
    fprintf(stderr, "UNLOCKING CLIENT: %p %p\n", cl, &cl->lock);
    print_trace();
    */
    int ret;
    if((ret = pthread_mutex_unlock(&cl->lock))) {
        LOG_ERROR(log_beep_main, "Unable to unlock client conn: %s",
                strerror(ret));
        abort();
    }
    /*
    fprintf(stderr, "CLIENT UNLOCKED: %p %p\n", cl, &cl->lock);
    */
}

static inline long client_hash(struct mg_connection *conn) {
    struct mg_request_info *req_info = mg_get_request_info(conn);
    return 17 * (5 + req_info->remote_ip) * (5 + req_info->remote_port);
}

static inline void add_client(struct messages_context *messages_ctx, struct client *cl) {
    lock_client_list(messages_ctx);

    list_add(&cl->head, &messages_ctx->client_list);
    messages_ctx->n_connections++;

    unlock_client_list(messages_ctx);
}

// Requires client_list_lock to be held
static inline void remove_client(struct messages_context *messages_ctx, struct client *cl) {
    //assert(messages_ctx->client_list_lock);

    list_del(&cl->head);
    messages_ctx->n_connections--;
}

// Returns a locked client.
static struct client *find_client(struct messages_context *messages_ctx, const char *sender_id,
        const char *app_id) {
    struct list_head *p;
    struct client *ret = NULL;

    lock_client_list(messages_ctx);

    struct client *cl;
    list_for_each(p, &messages_ctx->client_list) {
        cl = list_entry(p, struct client, head);
        if(!strcmp(cl->uuid, sender_id) && !strcmp(cl->app_name, app_id)) {
            ret = cl;
            lock_client(cl);
            goto out;
        }
    }

out:
    unlock_client_list(messages_ctx);
    return ret;
}

// Returns a locked client.
static struct client *find_client_by_hash(struct messages_context *messages_ctx, long hash) {
    struct list_head *p;
    struct client *ret = NULL;

    lock_client_list(messages_ctx);

    struct client *cl;
    list_for_each(p, &messages_ctx->client_list) {
        cl = list_entry(p, struct client, head);
        if(cl->hash == hash) {
            ret = cl;
            lock_client(ret);
            goto out;
        }
    }

out:
    unlock_client_list(messages_ctx);
    return ret;
}

static inline bool client_is_bound(struct client *cl) {
    return cl->state > CL_STATE_WAIT_BIND;
}

#define PATH_MAXLEN 32

// NOT SAFE TO CALL FROM UBUS THREAD
// always called with client->lock held.
static int client_invoke(struct client *cl, const char *method,
        struct blob_attr *params) {
    char path_buf[PATH_MAXLEN];
    struct blob_attr *response = NULL;
    struct messages_context *ctx = cl->messages_ctx;;
    int hash = cl->hash;

    snprintf(path_buf, PATH_MAXLEN, "beep.app.%s", cl->app_name);

    unlock_client(cl);

    int ret = beep_ubus_invoke(path_buf, method, params, &response);
    if(ret) {
        LOG_ERROR(log_beep_main, "Failed to send %s to %s: %s",
                method, path_buf, ubus_strerror(ret));
        // find_client locks client lock
        struct client *inner_cl = find_client_by_hash(ctx, hash);
        if (inner_cl) {
            finalize_client(inner_cl);
        }
    }

    //TODO: Parse out response for further errors
    free(response);
    return ret;
}

// NOT SAFE TO CALL FROM UBUS THREAD
// Always called with cl->lock held, released by client_invoke
static bool client_send_connected(struct client *cl) {
    struct blob_buf buf;

    memset(&buf, 0, sizeof(buf));
    blob_buf_init(&buf, 0);
    blobmsg_add_string(&buf, "sender_id", cl->uuid);
    blobmsg_add_string(&buf, "user_agent", cl->user_agent);

    int ret = client_invoke(cl, "msg_socket_sender_connected", buf.head);
    blob_buf_free(&buf);
    return(!!ret);
}

// NOT SAFE TO CALL FROM UBUS THREAD
// Always called with cl->lock held, released by client_invoke
static bool client_send_message(struct client *cl, const char *namespace,
        const char *message) {
    struct blob_buf buf;

    memset(&buf, 0, sizeof(buf));
    blob_buf_init(&buf, 0);
    blobmsg_add_string(&buf, "sender_id", cl->uuid);
    blobmsg_add_string(&buf, "namespace", namespace);
    blobmsg_add_string(&buf, "message", message);

    int ret = client_invoke(cl, "msg_socket_message_received", buf.head);
    blob_buf_free(&buf);
    return(!!ret);
}

static void sender_disconnected_cb(int ubus_ret,
        struct blob_attr *response, void *priv) {
    struct client *cl = (struct client *)priv;

    if(ubus_ret) {
        LOG_ERROR(log_beep_main, "Failed to send sender_disconnected");
    } else if(comm_flags.debug) {
        LOG_INFO(log_beep_main, "sender_disconnected sent for %s",
                cl->uuid);
    }

    free(cl->app_name);
    free(cl);
}

static void destroy_client(struct client *cl) {
    struct blob_buf args;
    char *path_buf;
    int ret;

    struct messages_context *ctx = cl->messages_ctx;

    lock_client_list(ctx);

    lock_client(cl);

    remove_client(ctx, cl);

    if(comm_flags.debug) {
        LOG_DEBUG(log_beep_main, "Finalizing client %s", cl->uuid);
    }

    uloop_timeout_cancel(&cl->t_hello);
    uloop_timeout_cancel(&cl->t_ping);
    uloop_timeout_cancel(&cl->t_pong);

    if(comm_flags.debug) {
        LOG_DEBUG(log_beep_main, "Timeouts cancelled %s", cl->uuid);
    }

    mg_websocket_write(cl->conn, WEBSOCKET_OPCODE_CONNECTION_CLOSE,
            NULL, 0);

    if(comm_flags.debug) {
        LOG_DEBUG(log_beep_main, "Close opcode %s", cl->uuid);
    }

    unlock_client(cl);

    unlock_client_list(ctx);

    ret = pthread_mutex_destroy(&cl->lock);

    if(ret) {
        LOG_ERROR(log_beep_main, "Unable to destroy mutex: %s",
                strerror(ret));
        abort();
    }

    LOG_INFO(log_beep_main, "%s removed. (%d active connections)",
        cl->uuid, cl->messages_ctx->n_connections);

    if(client_is_bound(cl)) {
        ret = asprintf(&path_buf, "beep.app.%s", cl->app_name);
        if(ret < 0) {
            LOG_ERROR(log_beep_main, "asprintf failed (OOM)");
            abort();
        }

        memset(&args, 0, sizeof(struct blob_buf));
        blob_buf_init(&args, 0);
        blobmsg_add_string(&args, "sender_id", cl->uuid);

        beep_ubus_invoke_async(path_buf, "msg_socket_sender_disconnected",
                args.head, 5000, &sender_disconnected_cb, cl);

        blob_buf_free(&args);
        free(path_buf);
    } else {
        free(cl->app_name);
        free(cl);
    }

}

// must be called with cl->lock held.
static void finalize_client(struct client *cl) {
    if(cl->state == CL_STATE_DESTROY_PENDING) { // Already pending destruction
        unlock_client(cl);
        return;
    }

    cl->state = CL_STATE_FINALIZING;

    unlock_client(cl);

    uint64_t val = 1;
    write(_messages_ctx->client_event_fd.fd, &val, 8);
}

/*
 * message container and __control__ message policies
 */
enum {
    MESSAGE_NAMESPACE,
    MESSAGE_MESSAGE,
    __MESSAGE_MAX
};

static const struct blobmsg_policy message_policy[] = {
    POLICY(MESSAGE_NAMESPACE, "namespace", BLOBMSG_TYPE_STRING),
    POLICY(MESSAGE_MESSAGE, "message", BLOBMSG_TYPE_STRING),
};

enum {
    CONTROL_MESSAGE_TYPE,
    CONTROL_MESSAGE_USER_AGENT,
    CONTROL_MESSAGE_APP,
    __CONTROL_MESSAGE_MAX
};

static const struct blobmsg_policy control_message_policy[] = {
    POLICY(CONTROL_MESSAGE_TYPE, "type", BLOBMSG_TYPE_STRING),
    POLICY(CONTROL_MESSAGE_USER_AGENT, "user_agent", BLOBMSG_TYPE_STRING),
    POLICY(CONTROL_MESSAGE_APP, "app", BLOBMSG_TYPE_STRING),
};

/*
 * Returns number of missing fields in tb based on policy (prints error messages
 * for missing attributes)
 */
static int verify_complete_policy(const struct blobmsg_policy *policy, int policy_len,
        struct blob_attr **tb) {
    int i, missing = 0;
    for(i = 0; i < policy_len; i++) {
        if(!tb[i]) {
            LOG_ERROR(log_beep_main, "Required message attribute %s not found",
                    policy[i].name);
            missing++;
        }
    }

    return missing;
}

/*
 * process_message is called from the read callback on each client connection.
 * We receive a string which should contain a json string with attributes
 * defined by the beep js api document (TODO: link)
 */
// Always called with cl->lock held
static void process_message(struct client *cl, const char *namespace,
        const char *message) {
    if(comm_flags.debug) {
        LOG_INFO(log_beep_main, "%s -> %s %s:\"%s\"", cl->uuid,
                cl->app_name, namespace, message);
    }

    client_send_message(cl, namespace, message);
}

#define PING_INTERVAL 10000
#define PONG_TIMEOUT  7000 // Must be smaller than PING_INTERVAL

static void pong_cb(struct uloop_timeout *t) {
    struct client *cl =
        container_of(t, struct client, t_pong);

    if(find_client_by_hash(_messages_ctx, cl->hash)) {
        if(!cl->pong) {
            LOG_WARN(log_beep_main, "%s failed to respond to PING in time.  Closing...",
                    cl->uuid);
            finalize_client(cl);
        } else {
            unlock_client(cl);
        }
    }
}

static void send_ping(struct client *cl) {
    if(find_client_by_hash(_messages_ctx, cl->hash)) {
        cl->pong = false;
        mg_websocket_write(cl->conn, WEBSOCKET_OPCODE_PING, NULL, 0);
        uloop_timeout_set(&cl->t_pong, PONG_TIMEOUT);
        unlock_client(cl);
    }
}

static void ping_cb(struct uloop_timeout *t) {
    struct client *cl =
        container_of(t, struct client, t_ping);

    send_ping(cl);
    uloop_timeout_set(t, PING_INTERVAL);
}

/*
 * client_read_cb is called any time there are available bytes in the client
 * connection stream.  We consume bytes from the stream one message frame at
 * a time.  Valid messages are sent to process_message.  Errors in this layer
 * terminate the connection.
 */

#define CONTROL_NAMESPACE "castchat.org:control"

#define MIN(x,y) ((x) > (y) ? (y) : (x))

static int ws_data(struct mg_connection *conn, int bits,
        char *data, size_t data_len) {
    char msg_buf[MSG_MAXLEN];
    char hdr_buf[MSG_HEADER_MAXLEN];

    header_t *hdr = NULL;
    char *dptr = NULL;
    int msg_len, hdr_len;
    const char *namespace, *type, *user_agent;

    const struct mg_request_info *req_info = mg_get_request_info(conn);
    struct messages_context *messages_ctx =
        (struct messages_context *)req_info->user_data;

    // client lock is locked by find_client
    struct client *cl = find_client_by_hash(messages_ctx, client_hash(conn));

    if(!cl) {
        return 0; // Tell mongoose to close this connection
    }

    if((bits & 0xF) == WEBSOCKET_OPCODE_CONNECTION_CLOSE) { // Client closed connection
        LOG_WARN(log_beep_main, "Received connection close flag");
        goto end_connection;
    } else if((bits & 0xF) == WEBSOCKET_OPCODE_PONG) {
        cl->pong = true;
        unlock_client(cl);
        return 1;
    } else if(data_len == 0) {
        LOG_WARN(log_beep_main, "No data");
        goto end_connection;
    }

    /*
     * Process header
     */
    dptr = strnstr(data,"\n\n",MIN(data_len, MSG_HEADER_MAXLEN));
    if(!dptr) {
        LOG_DEBUG(log_beep_main, "Incomplete message: Need more bytes");
        goto end_connection;
    } else if(dptr == data) {
        LOG_WARN(log_beep_main, "Malformed message: Empty header");
        goto end_connection;
    }

    hdr_len = dptr - data + 2;
    strncpy(hdr_buf, data, hdr_len);
    hdr_buf[hdr_len] = 0;

    hdr = header_parse(hdr_buf);

    msg_len = atoi(header_get(hdr, "Content-Length"));
    namespace = header_get(hdr, "Namespace");
    type = header_get(hdr, "Type");
    user_agent = header_get(hdr, "User-Agent");

    if(!namespace) {
        LOG_WARN(log_beep_main, "Missing required header field: Namespace");
        goto end_connection;
    }

    if(!client_is_bound(cl)) {
        if(strcmp(namespace, CONTROL_NAMESPACE)) {
            LOG_WARN(log_beep_main, "Expected control message from unbound client");
            goto end_connection;
        }

        if(msg_len != 0) {
            LOG_WARN(log_beep_main,
                    "Content-Length for control message MUST be 0");
            goto end_connection;
        }

        if(!type) {
            LOG_WARN(log_beep_main,
                    "Missing required header field for control message: "
                    "Type");
            goto end_connection;
        } else if(strcmp(type, "Hello")) {
            LOG_WARN(log_beep_main,
                    "Expected control message Type to be \"Hello\" "
                    "for unbound client");
            goto end_connection;
        }

        if(!user_agent) {
            LOG_WARN(log_beep_main,
                    "Missing required header field for control message: "
                    "User-Agent");
            goto end_connection;
        }

        strncpy(cl->user_agent, user_agent, 64);

        cl->state = CL_STATE_BOUND;

        // client is unlocked after this call
        client_send_connected(cl); // Blocking ubus call

        LOG_INFO(log_beep_main, "Client %s is bound", cl->uuid);

        // Trigger event
        uint64_t val = 1;
        write(messages_ctx->client_event_fd.fd, &val, 8);

    } else { // Client has bound
        if(msg_len < 1 || msg_len >= MSG_MAXLEN - 1) {
            LOG_WARN(log_beep_main, "Malformed message: Invalid length (%d)",
                    msg_len);
            goto end_connection;
        }

        dptr += 2; // Skip \n\n that mark end of header
        if(strnstr(dptr + msg_len, "\n\n", 2) != dptr + msg_len) {
            LOG_WARN(log_beep_main,
                    "Malformed message: Invalid terminator sequence");
            // [[Nick]]
            // BEEP-333 iOS Pandora app @ 29 Dec 2014 seems to be sending
            // incorrect content length (Consistently sending content that is
            // 2 bytes longer than content-length.
            //
            // Temporary fix: Attempt to find \n\n after dptr + msg_len, then
            // adjust msg_len accordingly
            //
            // TODO: Mongoose handles websocket message framing, so castchat's
            // required Content-Length field may be unnecessary.  We would be
            // able to assume that everything after the castchat header is data
            {
                char *msg_end;
                int old_msg_len;
                if(dptr + msg_len < data + data_len &&
                        (msg_end = strnstr(dptr + msg_len, "\n\n",
                        (data + data_len) - (dptr + msg_len))) != NULL) {
                    old_msg_len = msg_len;
                    msg_len = msg_end - dptr;
                    LOG_INFO(log_beep_main, "Fixed msg_len per BEEP-333: " \
                            "old: %d new: %d", old_msg_len, msg_len);
                } else {
                    LOG_ERROR(log_beep_main, "Unrecoverable message per " \
                            "BEEP-333");
                    goto end_connection;
                }
            }
        }

        msg_buf[msg_len] = 0;
        strncpy(msg_buf, dptr, msg_len);

        process_message(cl, namespace, msg_buf);
    }

    header_free(hdr);
    return 1;

end_connection:

    header_free(hdr);
    finalize_client(cl);

    return 0;
}

static void hello_timeout_cb(struct uloop_timeout *t) {
    struct client *cl =
        container_of(t, struct client, t_hello);

    if(!client_is_bound(cl)) {
        LOG_WARN(log_beep_main, "%s failed to send hello in time.  Closing...",
                cl->uuid);
        find_client_by_hash(_messages_ctx, cl->hash);
        finalize_client(cl);
    }
}

static void ws_ready(struct mg_connection *conn) {
    struct in_addr addr;
    pthread_mutexattr_t Attr;

    const struct mg_request_info *req_info = mg_get_request_info(conn);
    struct messages_context *messages_ctx =
        (struct messages_context *)req_info->user_data;

    struct client *new_client = calloc(1, sizeof(struct client));

    new_client->messages_ctx = messages_ctx;
    new_client->app_name = strdup(extract_app_name(req_info->uri));

    /*
     * get_session_id is a blocking ubus call
     */
    new_client->session_id = get_session_id(new_client->app_name);
    if(new_client->session_id <= 0) {
        /*
         * Free the pending client and return immediately.  ws_data is called
         * but client cannot be found and closes connection.
         */
        LOG_ERROR(log_beep_main, "Connection attempt to "
                "stopped/invalid app %s", new_client->app_name);
        free(new_client->app_name);
        free(new_client);
        return;
    }

    /*
     * app is running, finish instantiating client
     */
    uuid_t _uuid;
    uuid_generate(_uuid);
    uuid_unparse_lower(_uuid, new_client->uuid);
    new_client->state = CL_STATE_CONNECTED;
    new_client->t_ping.cb = &ping_cb;
    new_client->t_pong.cb = &pong_cb;
    new_client->t_hello.cb = &hello_timeout_cb;
    new_client->conn = conn;
    new_client->hash = client_hash(conn);
    pthread_mutexattr_init(&Attr);
    pthread_mutexattr_settype(&Attr, PTHREAD_MUTEX_ERRORCHECK);
    int ret = pthread_mutex_init(&new_client->lock, &Attr);
    if(ret) {
        LOG_ERROR(log_beep_main, "Unable to init mutex: %s", strerror(ret));
        abort();
    }

    addr.s_addr = req_info->remote_ip;
    LOG_INFO(log_beep_main, "Client %s connected from %s:%d "
            "(%d active connections)",
            new_client->uuid,
            inet_ntoa(addr),
            ntohs(req_info->remote_port), messages_ctx->n_connections);

    add_client(messages_ctx, new_client);

    /*
     * Adds client event to queue;  This client will be in CL_STATE_CONNECTED,
     * and the hello timer will be installed.
     */
    uint64_t val = 1;
    write(messages_ctx->client_event_fd.fd, &val, 8);

}

/*
 * ubus methods and object
 */

enum {
    APP_SEND_MESSAGE_APP_ID,
    APP_SEND_MESSAGE_SENDER_ID,
    APP_SEND_MESSAGE_NAMESPACE,
    APP_SEND_MESSAGE_MESSAGE,
    __APP_SEND_MESSAGE_MAX
};

static const struct blobmsg_policy app_send_message_policy[] = {
    [APP_SEND_MESSAGE_APP_ID] = { .name = "app_id", .type = BLOBMSG_TYPE_STRING },
    [APP_SEND_MESSAGE_SENDER_ID] = { .name = "sender_id", .type = BLOBMSG_TYPE_STRING },
    [APP_SEND_MESSAGE_NAMESPACE] = { .name = "namespace", .type = BLOBMSG_TYPE_STRING },
    [APP_SEND_MESSAGE_MESSAGE] = { .name = "message", .type = BLOBMSG_TYPE_STRING },
};

enum {
    APP_CLOSE_APP_ID,
    APP_CLOSE_SENDER_ID,
    __APP_CLOSE_MAX
};

static const struct blobmsg_policy app_close_policy[] = {
    [APP_CLOSE_APP_ID] = { .name = "app_id", .type = BLOBMSG_TYPE_STRING },
    [APP_CLOSE_SENDER_ID] = { .name = "sender_id", .type = BLOBMSG_TYPE_STRING },
};

enum {
    APP_CLOSE_ALL_APP_ID,
    __APP_CLOSE_ALL_MAX
};

static const struct blobmsg_policy app_close_all_policy[] = {
    [APP_CLOSE_ALL_APP_ID] = { .name = "app_id", .type = BLOBMSG_TYPE_STRING },
};

static int app_send_message(struct ubus_context *ubus_ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method, struct blob_attr *msg) {
    char msg_buf[MSG_MAXLEN];
    struct blob_attr *tb[__APP_SEND_MESSAGE_MAX];

    blobmsg_parse(app_send_message_policy, __APP_SEND_MESSAGE_MAX, tb,
            blob_data(msg), blob_len(msg));

    if(verify_complete_policy(app_send_message_policy, __APP_SEND_MESSAGE_MAX, tb)) {
        beep_reply_error(ubus_ctx, req, "Invalid request (see beepcomm output)",
                BEEP_UBUS_ERROR_ARGS);
        return 0;
    }

    char *app_id = blobmsg_data(tb[APP_SEND_MESSAGE_APP_ID]);
    char *sender_id = blobmsg_data(tb[APP_SEND_MESSAGE_SENDER_ID]);
    char *namespace = blobmsg_data(tb[APP_SEND_MESSAGE_NAMESPACE]);
    char *message = blobmsg_data(tb[APP_SEND_MESSAGE_MESSAGE]);

    // client lock is locked by find_client
    struct client *cl = find_client(_messages_ctx, sender_id, app_id);
    if(!cl) {
        beep_reply_error(ubus_ctx, req, "Sender not found", BEEP_UBUS_ERROR_ARGS);
        return 0;
    }

    size_t message_len = strlen(message);

    if(comm_flags.debug) {
        LOG_DEBUG(log_beep_main, "%s -> %s: %s\\n\\n", app_id, cl->uuid,
                message);
    }

    snprintf(msg_buf, MSG_MAXLEN - 1,
            "CASTCHAT/1.0\n"
            "Content-Length: %d\n"
            "Namespace: %s\n"
            "\n"
            "%s\n\n",
            (int)message_len, namespace, message);

    int wr = mg_websocket_write(cl->conn, WEBSOCKET_OPCODE_TEXT,
            msg_buf, strlen(msg_buf));
    unlock_client(cl);

    if(wr <= 0) {
        beep_reply_error(ubus_ctx, req, "Couldn't write to sender", -1);
        return 0;
    }

    beep_reply_success(ubus_ctx, req, NULL);
    return 0;
}

static int app_close(struct ubus_context *ubus_ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method, struct blob_attr *msg) {
    struct blob_attr *tb[__APP_CLOSE_MAX];

    blobmsg_parse(app_close_policy, __APP_CLOSE_MAX, tb,
            blob_data(msg), blob_len(msg));

    if(verify_complete_policy(app_close_policy, __APP_CLOSE_MAX, tb)) {
        beep_reply_error(ubus_ctx, req, "Invalid request (see beepcomm output)",
                BEEP_UBUS_ERROR_ARGS);
        return 0;
    }

    char *app_id = blobmsg_data(tb[APP_CLOSE_APP_ID]);
    char *sender_id = blobmsg_data(tb[APP_CLOSE_SENDER_ID]);

    // client lock is locked by find_client
    struct client *cl = find_client(_messages_ctx, sender_id, app_id);
    if(!cl) {
        beep_reply_error(ubus_ctx, req, "Sender not found", BEEP_UBUS_ERROR_ARGS);
        return 0;
    }

    // unlocks client lock
    finalize_client(cl);

    beep_reply_success(ubus_ctx, req, NULL);

    return 0;
}

static int app_close_all(struct ubus_context *ubus_ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method, struct blob_attr *msg) {
    struct blob_attr *tb[__APP_CLOSE_ALL_MAX];

    blobmsg_parse(app_close_all_policy, __APP_CLOSE_ALL_MAX, tb,
            blob_data(msg), blob_len(msg));

    if(verify_complete_policy(app_close_all_policy, __APP_CLOSE_ALL_MAX, tb)) {
        beep_reply_error(ubus_ctx, req, "Invalid request (see beepcomm output)",
                BEEP_UBUS_ERROR_ARGS);
        return 0;
    }

    char *app_id = blobmsg_data(tb[APP_CLOSE_ALL_APP_ID]);

    struct list_head *p, *n;
    struct client *cl;

    lock_client_list(_messages_ctx);

    list_for_each_safe(p, n, &_messages_ctx->client_list) {
        cl = list_entry(p, struct client, head);
        if(!strcmp(cl->app_name, app_id)) {
            lock_client(cl);
            finalize_client(cl);
        }
    }

    unlock_client_list(_messages_ctx);

    beep_reply_success(ubus_ctx, req, NULL);

    return 0;
}

static void client_event_cb(
        struct uloop_fd *u, unsigned int events) {
    struct messages_context *messages_ctx =
        container_of(u, struct messages_context, client_event_fd);
    struct list_head *p, *n;
    struct client *cl;
    uint64_t val;

    if(comm_flags.debug) {
        LOG_DEBUG(log_beep_main, "Client event CB");
    }

    read(u->fd, &val, 8); // Consume read event

    // TODO: do we need to lock the client list?
    list_for_each_safe(p, n, &messages_ctx->client_list) {
        cl = list_entry(p, struct client, head);
        switch(cl->state) {
            case CL_STATE_CONNECTED:
                if(find_client_by_hash(_messages_ctx, cl->hash)) {
                    uloop_timeout_set(&cl->t_hello, HELLO_TIMEOUT);
                    cl->state = CL_STATE_WAIT_BIND;
                    unlock_client(cl);
                }
                return;

            case CL_STATE_BOUND:
                uloop_timeout_cancel(&cl->t_hello);
                uloop_timeout_set(&cl->t_ping, PING_TIMEOUT);
                cl->state = CL_STATE_ACTIVE;
                return;

            case CL_STATE_FINALIZING:
                cl->state = CL_STATE_DESTROY_PENDING;
                destroy_client(cl);
                return;

            case CL_STATE_NEW:
            case CL_STATE_WAIT_BIND:
            case CL_STATE_ACTIVE:
            case CL_STATE_DESTROY_PENDING:
            default:
                break;
        }
    }
}

static const struct ubus_method comm_methods[] = {
    UBUS_METHOD("msg_socket_send_message", app_send_message, app_send_message_policy),
    UBUS_METHOD("msg_socket_close", app_close, app_close_policy),
    UBUS_METHOD("msg_socket_close_all", app_close_all, app_close_all_policy),
};

static struct ubus_object_type comm_type;

static struct ubus_object comm_object;

/*
 * module interface:
 *
 *   start_messages(void) starts listening and returns a messages_context
 *
 *   stop_messages(struct messages_context) releases resources and stops
 *       listening
 */

struct add_object_wrapper {
    struct messages_context *messages_ctx;
    start_messages_cb_t cb;
    void *priv;
};

void comm_object_added(struct ubus_context *ctx,
        struct ubus_object *obj, void *priv) {
    struct mg_callbacks callbacks;

    char port_str[10] = {0,};
    snprintf(port_str, 10, "%d", comm_flags.msg_port);

    const char *options[] = {
        "listening_ports", port_str,
        "num_threads", "8",
        NULL
    };

    struct add_object_wrapper *wrapper = (struct add_object_wrapper *)priv;
    struct messages_context *messages_ctx = wrapper->messages_ctx;
    start_messages_cb_t cb = wrapper->cb;
    void *outer_priv = wrapper->priv;

    free(wrapper);

    // Start mongoose
    memset(&callbacks, 0, sizeof(struct mg_callbacks));
    callbacks.websocket_ready = ws_ready;
    callbacks.websocket_data = ws_data;
    messages_ctx->mg_ctx = mg_start(&callbacks, messages_ctx, options);
    if(!messages_ctx->mg_ctx) {
        LOG_ERROR(log_beep_main, "Couldn't start mongoose");
        abort();
    }

    // Create client event fd
    messages_ctx->client_event_fd.cb = &client_event_cb;
    messages_ctx->client_event_fd.fd = eventfd(0, EFD_SEMAPHORE);
    if(messages_ctx->client_event_fd.fd == -1) {
        LOG_ERROR(log_beep_main, "Error creating eventfd: %s", strerror(errno));
        abort();
    }
    uloop_fd_add(&messages_ctx->client_event_fd, ULOOP_READ);

    LOG_INFO(log_beep_main,
            "messages API listening at http://localhost:%d/castchat/*",
            comm_flags.msg_port);

    if(cb) {
        cb(messages_ctx, outer_priv);
    }
}

void start_messages(struct ubus_context *ubus_ctx,
        start_messages_cb_t cb, void *priv) {
    pthread_mutexattr_t Attr;

    // Initialize the messages context
    struct messages_context *messages_ctx = calloc(1, sizeof(struct messages_context));
    messages_ctx->client_list.next = &messages_ctx->client_list;
    messages_ctx->client_list.prev = &messages_ctx->client_list;

    // Create client list lock
    pthread_mutexattr_init(&Attr);
    pthread_mutexattr_settype(&Attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(&messages_ctx->client_list_lock, &Attr);

    _messages_ctx = messages_ctx;
    messages_ctx->ubus_ctx = ubus_ctx;

    messages_ctx->n_connections = 0;

    comm_type.name = "beep.comm";
    comm_type.id = 0;
    comm_type.methods = comm_methods;
    comm_type.n_methods = ARRAY_SIZE(comm_methods);

    comm_object.name = "beep.comm";
    comm_object.type = &comm_type;
    comm_object.methods = comm_methods;
    comm_object.n_methods = ARRAY_SIZE(comm_methods);

    struct add_object_wrapper *wrapper = calloc(1, sizeof(struct add_object_wrapper));
    wrapper->cb = cb;
    wrapper->priv = priv;
    wrapper->messages_ctx = messages_ctx;

    int ret = ubus_add_object_async(ubus_ctx, &comm_object, &comm_object_added,
            wrapper);

    if(ret) {
        LOG_ERROR(log_beep_main, "Failed to add object: %s", ubus_strerror(ret));
        abort();
    }
}

struct remove_object_wrapper {
    struct messages_context *messages_ctx;
    stop_messages_cb_t cb;
    void *priv;
};

static void remove_object_callback(void *priv) {
    struct remove_object_wrapper *wrapper =
            (struct remove_object_wrapper *)priv;

    struct messages_context *messages_ctx = wrapper->messages_ctx;
    stop_messages_cb_t cb = wrapper->cb;
    void *outer_priv = wrapper->priv;

    struct list_head *p, *n;
    struct client *cl;

    free(wrapper);

    lock_client_list(messages_ctx);

    list_for_each_safe(p, n, &messages_ctx->client_list) {
        cl = list_entry(p, struct client, head);
        lock_client(cl);
        finalize_client(cl);
    }

    unlock_client_list(messages_ctx);

    pthread_mutex_destroy(&messages_ctx->client_list_lock);

    uloop_fd_delete(&messages_ctx->client_event_fd);
    close(messages_ctx->client_event_fd.fd);

    LOG_INFO(log_beep_main, "messages API stopped");

    free(messages_ctx);

    if(cb) {
        cb(outer_priv);
    }
}

void stop_messages(struct messages_context *messages_ctx,
        stop_messages_cb_t cb, void *priv) {
    mg_stop(messages_ctx->mg_ctx);

    LOG_WARN(log_beep_main, "MESSAGES API STOPPING");
    struct remove_object_wrapper *wrapper =
            calloc(1, sizeof(struct remove_object_wrapper));
    wrapper->messages_ctx = messages_ctx;
    wrapper->cb = cb;
    wrapper->priv = priv;

    ubus_remove_object_async(messages_ctx->ubus_ctx, &comm_object,
            &remove_object_callback, wrapper);
}
