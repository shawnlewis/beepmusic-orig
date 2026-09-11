#include "urelay.h"

#include <assert.h>

// Held in outgoing_client.connect_requests
struct connect_request {
    struct list_head head;

    // a deferred ubus urelay.connect request.
    struct ubus_request_data req;
};

struct outgoing_client {
    struct list_head head;

    char *ip;
    uint16_t port;
    char *id;

    struct uloop_fd uloop_connect_fd;
    struct client cl;

    // False while connecting, then true when connection achieved.
    bool is_connected;

    // A list of ubus requests that are waiting for the result of a
    // urelay.connect
    struct list_head connect_request_list;
};

struct remote_invoke {
    struct list_head head;

    uint32_t id;
    struct ubus_request_data def_req;
};

struct list_head oc_list = LIST_HEAD_INIT(oc_list);
struct list_head ri_list = LIST_HEAD_INIT(ri_list);

static uint32_t msg_id = 0;

/*
 * method policies for basic methods -- connect, disconnect, is_connected
 */
enum {
    CONNECT_IP,
    CONNECT_PORT,
    CONNECT_ID,
    __CONNECT_MAX
};

const struct blobmsg_policy connect_policy[] = {
    [CONNECT_IP] = { .name = "ip", .type = BLOBMSG_TYPE_STRING },
    [CONNECT_PORT] = { .name = "port", .type = BLOBMSG_TYPE_INT32 },
    [CONNECT_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING }
};

enum {
    DISCONNECT_ID,
    __DISCONNECT_MAX
};

const struct blobmsg_policy disconnect_policy[] = {
    [DISCONNECT_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING }
};

enum {
    IS_CONNECTED_ID,
    __IS_CONNECTED_MAX
};

const struct blobmsg_policy is_registered_policy[] = {
    [IS_CONNECTED_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING }
};

/*
 * method policy for invoke method
 */
enum {
    INVOKE_ID,
    INVOKE_PATH,
    INVOKE_METHOD,
    INVOKE_MSG,
    __INVOKE_MAX
};

const struct blobmsg_policy invoke_policy[] = {
    [INVOKE_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
    [INVOKE_PATH] = { .name = "path", .type = BLOBMSG_TYPE_STRING },
    [INVOKE_METHOD] = { .name = "method", .type = BLOBMSG_TYPE_STRING },
    [INVOKE_MSG] = { .name = "msg", .type = BLOBMSG_TYPE_TABLE }
};

static struct remote_invoke *lookup_request(uint32_t id)
{
    struct list_head *p;
    list_for_each(p, &ri_list) {
        struct remote_invoke *ri = list_entry(p, struct remote_invoke, head);
        if(ri->id == id)
            return ri;
    }

    return NULL;
}

static void add_remote_invoke(struct remote_invoke *ri)
{
    list_add(&ri->head, &ri_list);
}

static struct remote_invoke *pop_remote_invoke(uint32_t id)
{
    struct remote_invoke *ri = lookup_request(id);
    list_del(&ri->head);
    return ri;
}

static struct outgoing_client *lookup_client(char *id)
{
    struct list_head *p;
    list_for_each(p, &oc_list) {
        struct outgoing_client *oc =
            list_entry(p, struct outgoing_client, head);
        if(!strcmp(oc->id,id))
            return oc;
    }

    return NULL;
}

static void add_outgoing_client(struct outgoing_client *cl)
{
    list_add(&cl->head, &oc_list);
}

static void del_outgoing_client(struct outgoing_client *cl)
{
    if(lookup_client(cl->id)) {
        list_del(&cl->head);
        if (cl->is_connected) {
            ustream_free(&cl->cl.us_fd.stream);
            close(cl->cl.us_fd.fd.fd);
        } else {
            uloop_fd_delete(&cl->uloop_connect_fd);
            close(cl->uloop_connect_fd.fd);
        }
        free(cl->ip);
        free(cl->id);
        struct connect_request *cr, *cr_next;
        list_for_each_entry_safe(cr, cr_next, &cl->connect_request_list, head) {
            free(cr);
        }
        free(cl);
    }
}

static void handle_response(const struct blob_attr *sbuf) {
    struct blob_attr *tb[_BLOB_RESPONSE_LENGTH];
    struct remote_invoke *ri = NULL;
    struct blob_buf buf;

    if(blobmsg_parse(response_blob_policy, _BLOB_RESPONSE_LENGTH,
            tb, blob_data(sbuf), blob_len(sbuf))) {
        LOG_ERROR(log_beep_main, "Failed to parse response blob");
        abort();
    }

    uint32_t id = blobmsg_get_u32(tb[BLOB_RESPONSE_ID]);
    memset(&buf, 0, sizeof(buf));
    blob_buf_init(&buf, 0);

    if(tb[BLOB_RESPONSE_MSG]) {
        struct blob_attr *msg =
            blobmsg_data(tb[BLOB_RESPONSE_MSG]);
        struct blob_attr *attr;
        int len = blobmsg_data_len(tb[BLOB_RESPONSE_MSG]);

        __blob_for_each_attr(attr, msg, len)
            blobmsg_add_blob(&buf, attr);
    }

    ri = pop_remote_invoke(id);

    beep_reply_success(ctx,&ri->def_req,buf.head);
    blob_buf_free(&buf);
    ubus_complete_deferred_request(ctx,&ri->def_req, 0);
    free(ri);
}

static void handle_error(const struct blob_attr *sbuf) {
    struct blob_attr *tb[_BLOB_ERROR_LENGTH];
    struct blob_buf buf;
    struct remote_invoke *ri = NULL;

    if(blobmsg_parse(error_blob_policy, _BLOB_ERROR_LENGTH,
            tb, blob_data(sbuf), blob_len(sbuf))) {
        LOG_ERROR(log_beep_main, "Failed to parse error blob");
        abort();
    }

    uint32_t id = blobmsg_get_u32(tb[BLOB_ERROR_ID]);
    char *error_msg = blobmsg_data(tb[BLOB_ERROR_MSG]);
    int error_code = blobmsg_get_u32(tb[BLOB_ERROR_CODE]);
    memset(&buf, 0, sizeof(buf));
    blob_buf_init(&buf, 0);

    ri = pop_remote_invoke(id);

    beep_reply_error(ctx,&ri->def_req,error_msg,error_code);
    blob_buf_free(&buf);
    ubus_complete_deferred_request(ctx,&ri->def_req, 0);
    free(ri);
}

/*
 * TODO: This implementation must be synchronized with the
 * beep_ubus.h implementation.
 */

static void handle_event(const struct blob_attr *sbuf,
        const struct outgoing_client *cl)
{
    struct blob_attr *tb[_BLOB_EVENT_LENGTH];
    struct blob_buf buf;

    if(blobmsg_parse(event_blob_policy, _BLOB_EVENT_LENGTH,
            tb, blob_data(sbuf), blob_len(sbuf))) {
        LOG_ERROR(log_beep_main, "Failed to parse event blob");
        abort();
    }

    char *ev_type = blobmsg_data(tb[BLOB_EVENT_TYPE]);
    char id[BEEP_UBUS_EVENT_ID_MAX_LENGTH];

    snprintf(id, BEEP_UBUS_EVENT_ID_MAX_LENGTH,"%s%s.%s",
            EVENT_PREFIX, ev_type, cl->id);

    memset(&buf, 0, sizeof(buf));
    blob_buf_init(&buf, 0);

    struct blob_attr *msg = blobmsg_data(tb[BLOB_EVENT_MSG]);
    struct blob_attr *attr;
    int len = blobmsg_data_len(tb[BLOB_EVENT_MSG]);

    __blob_for_each_attr(attr, msg, len)
        blobmsg_add_blob(&buf, attr);

    int ret = beep_ubus_send_event(ctx, id, buf.head);
    if(ret) {
        char* blob_json_str = blobmsg_format_json(buf.head, true);
        LOG_ERROR(log_beep_main,
                "Error %d passing event %s, %s", ret, id, blob_json_str);
        abort();
    }
    blob_buf_free(&buf);
}

/*
 * out_client_read_cb handles responses and events from other urelays
 */
static void out_client_read_cb(struct ustream *s, int bytes)
{
    struct outgoing_client *this_client =
        container_of(s, struct outgoing_client, cl.us_fd.stream);

    struct blob_attr *sbuf = NULL;
    int type;

    int read_len, raw_len;

    do {
        void* ubuf = ustream_get_read_buf(s, &read_len);

        if(read_len < sizeof(uint32_t) * 2)
            break;

        type = be32_to_cpu(*(uint32_t *)ubuf);
        sbuf = (struct blob_attr *) (ubuf + 4);

        raw_len = blob_raw_len(sbuf) + 4;

        if(debug)
            LOG_DEBUG(log_beep_main,
                    "Response read, available = %d, raw = %d",
                    read_len,raw_len);

        if(read_len < raw_len) // Buffer does not contain blob
            break;

        switch(type) {
            case BLOB_TYPE_RESPONSE:
                handle_response(sbuf);
                break;

            case BLOB_TYPE_ERROR:
                handle_error(sbuf);
                break;

            case BLOB_TYPE_EVENT:
                handle_event(sbuf, this_client);
                break;

            default:
                LOG_ERROR(log_beep_main, "Bad event type, %d", type);
                abort();
                break;
        }
        if(s->r.head != NULL) {
            ustream_consume(s, raw_len);
        }
    } while(1);
}

/*
 * out_client_state_cb called when stream state changes,
 * e.g., eof or write error.
 */
static void out_client_state_cb(struct ustream *s)
{
    struct outgoing_client *this_client =
        container_of(s, struct outgoing_client, cl.us_fd.stream);
    struct blob_buf buf;

    LOG_INFO(log_beep_main, "out_client_state_cb. eof: %d write_error: %d",
            s->eof, s->write_error);

    // Only interested in EOF or write_error
    if (!(s->eof || s->write_error))
        return;

    // Send device removed event
    memset(&buf, 0, sizeof(buf));
    blob_buf_init(&buf, 0);

    blobmsg_add_string(&buf, "id", this_client->id);
    blobmsg_add_string(&buf, "ip", this_client->ip);
    blobmsg_add_u32(&buf, "port", this_client->port);
    blobmsg_add_string(&buf, "received_by", "urelay");

    del_outgoing_client(this_client);

    beep_ubus_send_event(ctx, "beep.device.error", buf.head);

    blob_buf_free(&buf);
}

/*
 * runs when uloop_connect_fd becomes writable, meaning the socket is either
 * connected or the connection attempt has failed.
 */
void uloop_connect_fd_ready_cb(struct uloop_fd *u, unsigned int events) {
    struct outgoing_client *oc =
        container_of(u, struct outgoing_client, uloop_connect_fd);

    int error;
    socklen_t error_len = sizeof(error);
    getsockopt(u->fd, SOL_SOCKET, SO_ERROR, &error, &error_len);

    // Do this before adding it again via ustream_fd_init.
    uloop_fd_delete(u);

    // TODO: iterate over connect_request_list to reply
    struct connect_request* cr;
    if (error) {
        LOG_WARN(log_beep_main, "Connection to %s:%d failed: %s",
                oc->ip, oc->port, strerror(error));
        list_for_each_entry(cr, &oc->connect_request_list, head) {
            beep_reply_error(ctx, &cr->req, "Connect failed",
                    BEEP_UBUS_ERROR);
            ubus_complete_deferred_request(ctx, &cr->req, BEEP_UBUS_ERROR);
        }
        del_outgoing_client(oc);
    } else {
        oc->is_connected = true;
        ustream_fd_init(&oc->cl.us_fd, u->fd);
        list_for_each_entry(cr, &oc->connect_request_list, head) {
            beep_reply_success(ctx, &cr->req, NULL);
            ubus_complete_deferred_request(ctx, &cr->req, 0);
        }
        LOG_DEBUG(log_beep_main, "Connected to remote relay '%s'", oc->id);
    }
}

/*
 * ----------------
 * Method callbacks
 * ----------------
 */

/*
 * method: connect
 * params:
 *   ip                 (string) numeric ip address of remote urelay
 *
 * returns:
 *   (success)          if successfully connected to remote urelay;
 *   (error_code:0)     otherwise
 */
static int relay_connect(struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg)
{
    struct outgoing_client *oc;

    struct blob_attr *tb[__CONNECT_MAX];
    char *ip = NULL;

    uint32_t port = URELAY_PORT;
    char port_str[32];

    char *id = NULL;

    int fd;

    blobmsg_parse(connect_policy,
            ARRAY_SIZE(connect_policy), tb, blob_data(msg), blob_len(msg));

    if(!tb[CONNECT_IP]) {
        beep_reply_error(ctx, req, "Invalid/missing ip address",
                BEEP_UBUS_ERROR_ARGS);
        return 0;
    }
    ip = blobmsg_data(tb[CONNECT_IP]);

    if(!tb[CONNECT_ID]) {
        beep_reply_error(ctx, req, "Missing ID",
                BEEP_UBUS_ERROR_ARGS);
        return 0;
    }
    id = blobmsg_data(tb[CONNECT_ID]);

    if(tb[CONNECT_PORT])
        port = blobmsg_get_u32(tb[CONNECT_PORT]);

    if((oc = lookup_client(id)) && oc->is_connected) {
        // We return success if already connected.
        LOG_DEBUG(log_beep_main,
                "Connect requested but device already connected: %s", id);
        beep_reply_success(ctx, req, NULL);
        return 0;
    }

    if (oc) {
        LOG_DEBUG(log_beep_main,
                "Connected requested but device is already connecting: %s",
                id);
    } else {
        LOG_DEBUG(log_beep_main, "Connecting device id: %s %s %d", id, ip, port);
        sprintf(port_str, "%d", port);
        fd = usock(USOCK_NUMERIC | USOCK_TCP | USOCK_IPV4ONLY | USOCK_NONBLOCK
                   | USOCK_NOADDRCONFIG,
                ip, port_str);

        if(fd < 0) {
            LOG_ERROR(log_beep_main, "usock open failed");
            beep_reply_error(ctx, req, "Couldn\'t create socket", BEEP_UBUS_ERROR);
            return 0;
        }

        // Enable TCP keepalive
        int optval = 1;
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));

        // Probe count
        optval = TCP_PROBE_COUNT;
        setsockopt(fd, SOL_TCP, TCP_KEEPCNT, &optval, sizeof(optval));

        // Idle time
        optval = TCP_IDLE_TIME;
        setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &optval, sizeof(optval));

        // Interval
        optval = TCP_INTERVAL;
        setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, &optval, sizeof(optval));

        LOG_DEBUG(log_beep_main, "usock open succeeded");

        // Network performance optimization (this measurably improves performance
        // for Beep). Disable Nagle's algorithm by setting TCP_NODELAY.
        int flag = 1;
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                (char *) &flag, sizeof(int)))  {
            LOG_ERROR(log_beep_main, "setsockopt TCP_NODELAY failed: %s",
                    strerror(errno));
            exit(1);
        }

        oc = calloc(1, sizeof(struct outgoing_client));
        INIT_LIST_HEAD(&oc->connect_request_list);

        oc->ip = strdup(ip);
        oc->id = strdup(id);
        oc->port = port;

        oc->uloop_connect_fd.cb = uloop_connect_fd_ready_cb;
        oc->uloop_connect_fd.fd = fd;

        oc->cl.us_fd.stream.r.buffer_len = USTREAM_BUF_SIZE;
        oc->cl.us_fd.stream.string_data = false;
        oc->cl.us_fd.stream.notify_read = out_client_read_cb;
        oc->cl.us_fd.stream.notify_state = out_client_state_cb;

        add_outgoing_client(oc);

        uloop_fd_add(&oc->uloop_connect_fd, ULOOP_WRITE);
    }

    // Defer the request and add it to the list of requests that are waiting
    // for the connection to complete.
    struct connect_request* connect_request =
            calloc(1, sizeof(struct connect_request));
    ubus_defer_request(ctx, req, &connect_request->req);
    list_add(&connect_request->head, &oc->connect_request_list);

    return 0;
}

/*
 * method: disconnect
 * params:
 *   id            (string) id string provided in connect call
 *
 * returns:
 *   (success)
 */
static int relay_disconnect(struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg)
{
    struct blob_attr *tb[__DISCONNECT_MAX];
    struct outgoing_client *oc = NULL;

    char *id = NULL;

    blobmsg_parse(disconnect_policy, ARRAY_SIZE(disconnect_policy),
            tb, blob_data(msg), blob_len(msg));

    if(!tb[DISCONNECT_ID]) {
        beep_reply_error(ctx, req, "Must supply ID",
                BEEP_UBUS_ERROR_ARGS);
        return 0;
    }
    id = blobmsg_data(tb[DISCONNECT_ID]);

    if(!(oc = lookup_client(id))) {
        /*
         * This is not really an error, more like an invalid command.  In
         * any case, calling disconnect on a host that isn't connected
         * produces the same result as calling disconnect on a host that is.
         */
        LOG_DEBUG(log_beep_main,
                "Disconnect requested but device not found: '%s'", id);
        beep_reply_success(ctx, req, NULL);
        return 0;
    }

    LOG_DEBUG(log_beep_main, "Disconnecting from relay '%s'", oc->id);

    del_outgoing_client(oc);

    beep_reply_success(ctx, req, NULL);
    return 0;
}

/*
 * method: is_connected
 * params:
 *   id        (string) id string provided in connect call
 *
 * returns:
 *   (success)
 *   bool connected     true if this host is connected, false otherwise.
 */
static int relay_is_registered(struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg)
{
    struct blob_attr *tb[__IS_CONNECTED_MAX];
    struct blob_buf buf;

    char *id;

    blobmsg_parse(is_registered_policy, ARRAY_SIZE(is_registered_policy),
            tb, blob_data(msg), blob_len(msg));

    if(!tb[IS_CONNECTED_ID]) {
        beep_reply_error(ctx, req, "Must supply ID",
                BEEP_UBUS_ERROR_ARGS);
        return 0;
    }
    id = blobmsg_data(tb[IS_CONNECTED_ID]);

    memset(&buf, 0, sizeof(buf));
    blob_buf_init(&buf, 0);
    blobmsg_add_u8(&buf, "connected", lookup_client(id) ? true : false);
    beep_reply_success(ctx, req, buf.head);
    blob_buf_free(&buf);
    return 0;
}

static int relay_list_registered(struct ubus_context *ctx,
        struct ubus_object *obj, struct ubus_request_data *req,
        const char *method, struct blob_attr *msg)
{
    struct outgoing_client *oc;
    struct blob_buf buf;
    void *a;

    memset(&buf, 0, sizeof(buf));
    blob_buf_init(&buf, 0);
    a = blobmsg_open_array(&buf, "ids");
    list_for_each_entry(oc, &oc_list, head)
        blobmsg_add_string(&buf, NULL, oc->id);
    blobmsg_close_array(&buf, a);

    beep_reply_success(ctx, req, buf.head);
    blob_buf_free(&buf);
    return 0;
}
/*
 * method name: invoke
 *   host       (string) address of remote urelay
 *   path       (string) path of ubus object on remote device
 *   method     (string) method name to call on remote ubus object
 *   msg        (table)  message to pass as parameters to method
 *
 *  returns:
 *    (success)
 *    table msg         msg response from remote ubus objectA
 *
 *    TODO: errors
 */
static int relay_invoke(struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg)
{
    struct blob_attr *tb[__INVOKE_MAX];
    struct blob_buf buf;
    struct outgoing_client *oc = NULL;
    struct remote_invoke *ri = NULL;

    char *id;

    void *table;

    //LOG_DEBUG(log_beep_main, "Received msg: %s\n", blobmsg_format_json(msg, 1));

    blobmsg_parse(invoke_policy, ARRAY_SIZE(invoke_policy),
            tb, blob_data(msg), blob_len(msg));

    /*
     * Must provide id
     */
    if(!tb[INVOKE_ID]) {
        beep_reply_error(ctx, req, "Invalid/missing ID",
                BEEP_UBUS_ERROR_ARGS);
        return 0;
    }

    id = blobmsg_data(tb[INVOKE_ID]);

    /*
     * Must be connected to remote host
     */
    if(!((oc = lookup_client(id)) && oc->is_connected)) {
        beep_reply_error(ctx, req, "Host not connected", BEEP_UBUS_ERROR);
        return 0;
    }
    assert(oc->cl.us_fd.stream.write);

    /*
     * Validate parameters
     */
    if(!tb[INVOKE_PATH] || !tb[INVOKE_METHOD]) {
        beep_reply_error(ctx, req, "Missing path and/or method parameters",
                BEEP_UBUS_ERROR_ARGS);
        return 0;
    }

    /*
     * Build invoke blob
     */
    memset(&buf, 0, sizeof(buf));
    blob_buf_init(&buf, 0);
    blobmsg_add_u32(&buf, "id", msg_id++);
    blobmsg_add_string(&buf, "path", blobmsg_data(tb[INVOKE_PATH]));
    blobmsg_add_string(&buf, "method", blobmsg_data(tb[INVOKE_METHOD]));

    if(tb[INVOKE_MSG]) {
        table = blobmsg_open_table(&buf, "msg");
        struct blob_attr *msg = blobmsg_data(tb[INVOKE_MSG]);
        struct blob_attr *attr;
        int len = blobmsg_data_len(tb[INVOKE_MSG]);

        __blob_for_each_attr(attr, msg, len)
            blobmsg_add_blob(&buf, attr);

        blobmsg_close_table(&buf, table);
    }

    /*
     * Write to TCP stream
     */
    assert(blob_raw_len(buf.head) < USTREAM_BUF_SIZE);
    int wr = ustream_write(&oc->cl.us_fd.stream, (const char *)buf.head,
            blob_raw_len(buf.head), false);
    if(wr <= 0) {
        beep_reply_error(ctx, req, "Couldn't write to server", BEEP_UBUS_ERROR);
        return 0;
    } else if(debug)
        LOG_DEBUG(log_beep_main, "Wrote %d bytes", wr);

    blob_buf_free(&buf);
    /*
     * Create remote invoke container so we can retrieve deferred request when
     * request comes back.
     */
    ri = calloc(1, sizeof(struct remote_invoke));
    ri->id = msg_id - 1;
    ubus_defer_request(ctx, req, &ri->def_req);
    add_remote_invoke(ri);

    return 0;
}

/*
 * ubus object definition
 */
static const struct ubus_method relay_methods[] = {
    UBUS_METHOD("connect", relay_connect, connect_policy),
    UBUS_METHOD("disconnect", relay_disconnect, disconnect_policy),
    UBUS_METHOD("invoke", relay_invoke, invoke_policy),
    UBUS_METHOD("__is_registered", relay_is_registered, is_registered_policy),
    UBUS_METHOD_NOARG("__list_registered", relay_list_registered)
};

struct ubus_object_type relay_type =
    UBUS_OBJECT_TYPE(NULL, relay_methods);

struct ubus_object relay_object = {
    .name = NULL,
    .type = &relay_type,
    .methods = relay_methods,
    .n_methods = ARRAY_SIZE(relay_methods)
};
