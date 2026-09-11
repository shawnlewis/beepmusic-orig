#include "urelay.h"
#include <assert.h>

struct local_invoke {
    struct ustream *s;
    struct client *cl;

    uint32_t id;
};

uint16_t server_port = URELAY_PORT;
static struct uloop_fd server;

static struct list_head cl_list = LIST_HEAD_INIT(cl_list);

static void add_client(struct client *cl)
{
    list_add(&cl->head, &cl_list);
}

static void remove_client(struct client *cl) // Does not free the client
{
    list_del(&cl->head);
}

static void _free_client(struct client *cl);

static void bind_client(struct client *cl) {
    cl->n_refs++;
    if(debug)
        LOG_DEBUG(log_beep_main, "%p => %d", cl, cl->n_refs);
}

static void release_client(struct client *cl) {
    cl->n_refs--;
    if(debug)
        LOG_DEBUG(log_beep_main, "%p => %d", cl, cl->n_refs);
    _free_client(cl);
}

static void destroy_client(struct client *cl) {
    cl->destroyed = true;
    if(debug)
        LOG_DEBUG(log_beep_main, "%p DESTROYED", cl);
    _free_client(cl);
}


// Do not call this directly: use bind/release/destroy
static void _free_client(struct client *cl) {
    if(!cl->n_refs && cl->destroyed) {
        if(debug) {
            LOG_DEBUG(log_beep_main, "%p FREE", cl);
        }
        free(cl);
    }
}

static void invoke_async_cb(int ubus_ret, struct blob_attr *response, void *priv) {
    struct local_invoke *li = (struct local_invoke *)priv;
    struct blob_buf buf;
    struct blob_attr *tmsg, *attr;
    int wr, len;
    void *table;

    uint32_t resp_type;

    memset(&buf, 0, sizeof(buf));
    blob_buf_init(&buf, 0);

    if(ubus_ret) { // invoke failed
        LOG_ERROR(log_beep_main,
                "Error invoking local method: %s",
                ubus_strerror(ubus_ret));
        if(li->cl->destroyed) {
            LOG_DEBUG(log_beep_main,
                    "Client is destroyed -- not writing response");

            goto out;
        }

        blobmsg_add_u32(&buf, "id", li->id);
        blobmsg_add_u32(&buf, "code", ubus_ret);
        blobmsg_add_string(&buf, "error_msg", ubus_strerror(ubus_ret));

        resp_type = cpu_to_be32(BLOB_TYPE_ERROR);
        wr = ustream_write(li->s, (const char *)&resp_type,
                sizeof(uint32_t), true);
        wr += ustream_write(li->s, (const char *)buf.head,
                blob_raw_len(buf.head), false);

        if(wr <= 0)
            LOG_ERROR(log_beep_main, "Couldn't write to client");
        else if(debug)
            LOG_DEBUG(log_beep_main, "Wrote %d bytes",wr);

    } else { // invoke succeeded
        if(li->cl->destroyed) {
            LOG_DEBUG(log_beep_main,
                    "Client is destroyed -- not writing response");
            goto out;
        }

        // Write out response
        blobmsg_add_u32(&buf, "id", li->id);
        table = blobmsg_open_table(&buf, "msg");

        tmsg = blob_data(response);
        len = blob_len(response);
        __blob_for_each_attr(attr, tmsg, len)
            blobmsg_add_blob(&buf, attr);

        blobmsg_close_table(&buf, table);

        uint32_t resp_type = cpu_to_be32(BLOB_TYPE_RESPONSE);

        wr = ustream_write(li->s, (const char *)&resp_type, sizeof(uint32_t),
                true);
        wr += ustream_write(li->s, (const char *)buf.head,
                blob_raw_len(buf.head), false);

        if(wr <= 0)
            LOG_ERROR(log_beep_main, "Couldn't write to client");
        else if(debug)
            LOG_DEBUG(log_beep_main, "Wrote %d bytes",wr);
    }

out:
    blob_buf_free(&buf);
    release_client(li->cl);
    free(li);
}

/*
 * client_read_cb handles incoming data from other urelays
 * we are expecting a blob defined by the blobmsg_policy invoke_blob_policy
 */
static void client_read_cb(struct ustream *s, int bytes)
{
    struct local_invoke *li;
    struct client *cl = container_of(s, struct client, us_fd.stream);
    struct blob_attr *tb[_BLOB_INVOKE_LENGTH];

    struct blob_attr *sbuf = NULL;

    int read_len, raw_len;
    uint32_t id;
    char *path, *method;
    struct blob_buf buf;

    do {
        sbuf = (struct blob_attr *)ustream_get_read_buf(s, &read_len);
        if(read_len < sizeof(uint32_t))
            break;

        raw_len = blob_raw_len(sbuf);

        if(debug)
            LOG_DEBUG(log_beep_main, "Client read, available: %d, raw: %d",
                    read_len, raw_len);

        if(read_len < raw_len) // Buffer does not contain the whole blob
            break;

        if(blobmsg_parse(invoke_blob_policy, _BLOB_INVOKE_LENGTH,
                    tb, blob_data(sbuf),
               blob_len(sbuf))) {
            LOG_ERROR(log_beep_main,
                    "****** Failed to parse incoming invoke blob *******");
        } else {
            id = blobmsg_get_u32(tb[BLOB_INVOKE_ID]);
            path = blobmsg_data(tb[BLOB_INVOKE_PATH]);
            method = blobmsg_data(tb[BLOB_INVOKE_METHOD]);

            ustream_consume(s, raw_len);

            memset(&buf, 0, sizeof(buf));
            blob_buf_init(&buf, 0);
            if(tb[BLOB_INVOKE_MSG]) {
                struct blob_attr *msg = blobmsg_data(tb[BLOB_INVOKE_MSG]);
                struct blob_attr *attr;
                int msglen = blobmsg_data_len(tb[BLOB_INVOKE_MSG]);

                __blob_for_each_attr(attr, msg, msglen)
                    blobmsg_add_blob(&buf, attr);
            }

            li = calloc(1, sizeof(struct local_invoke));
            li->id = id;
            li->cl = cl;
            li->s = s;
            bind_client(cl);
            ubus_invoke_async_full(ctx, path, method, buf.head,
                    15000, &invoke_async_cb, li);
            blob_buf_free(&buf);
        }
    } while(1);
}

/*
 * client_state_cb called when stream state changes, e.g., eof or write error
 */
static void client_state_cb(struct ustream *s)
{
    struct client *this_client = container_of(s, struct client, us_fd.stream);
    if(this_client->destroyed) {
        return;
    }

    LOG_INFO(log_beep_main, "client state change. eof: %d, write_error: %d",
            s->eof, s->write_error);

    if(s->write_error || !s->w.data_bytes) {
        LOG_INFO(log_beep_main, "Connection closed");
        ustream_free(s);
        close(this_client->us_fd.fd.fd);
        remove_client(this_client);
        destroy_client(this_client);
    }
}

/*
 * This callback handles relaying local events to clients
 */

static void event_handler(struct ubus_context *ctx,
        struct ubus_event_handler *ev,
        const char *type, struct blob_attr *msg) {
    void *t;
    struct blob_buf buf;

    char *component_end = NULL;
    if(!(component_end = strstr(type,"._local_"))) {
        return;
    }

    memset(&buf, 0, sizeof(buf));
    blob_buf_init(&buf, 0);

    char component[128];
    int component_len = component_end - (type + EVENT_PREFIX_LENGTH);

    strncpy(component, type + EVENT_PREFIX_LENGTH, component_len);
    component[component_len] = 0;

    blobmsg_add_string(&buf, "type", component);
    if(!msg) {
        LOG_ERROR(log_beep_main,
                "event_handler caught malformed %s", type);
        abort();
    }

    struct blob_attr *tmsg = blob_data(msg);
    struct blob_attr *attr;
    int len = blob_len(msg);

    t = blobmsg_open_table(&buf, "msg");

    __blob_for_each_attr(attr, tmsg, len)
        blobmsg_add_blob(&buf, attr);

    blobmsg_close_table(&buf, t);

    struct list_head *p;
    struct client *cl;

    // TODO: ustream read buffer is set to this size on the client side.
    // Any data beyond 16k will be truncated, resulting in read errors.
    assert(blob_raw_len(buf.head) + sizeof(uint32_t) < USTREAM_BUF_SIZE);
    list_for_each(p, &cl_list) {
        cl = list_entry(p, struct client, head);
        uint32_t resp_type = cpu_to_be32(BLOB_TYPE_EVENT);
        int wr = ustream_write(&cl->us_fd.stream, (const char *)&resp_type,
                sizeof(uint32_t), true);
        wr += ustream_write(&cl->us_fd.stream, (const char *)buf.head,
                blob_raw_len(buf.head), false);
        if(wr <= 0)
            LOG_ERROR(log_beep_main, "Couldn't write to client %p",cl);
        else if(debug)
            LOG_DEBUG(log_beep_main, "Wrote %d bytes to client %p", wr, cl);
    }

    blob_buf_free(&buf);
}

static struct ubus_event_handler ev_handler_obj = {
    .cb = event_handler
};

/*
 * This callback sets up incoming client connections
 */
static void server_cb(struct uloop_fd *ufd, unsigned int events) {
    static struct client *incoming_client = NULL;

    unsigned int sl = sizeof(struct sockaddr_in);
    int new_fd;

    if(!incoming_client) {
        incoming_client = calloc(1, sizeof(*incoming_client));
    }

    new_fd = accept(server.fd, (struct sockaddr *) &incoming_client->cli_addr,
            &sl);
    if(new_fd < 0) {
        LOG_ERROR(log_beep_main, "Failed to accept connection: %s. Aborting...",
                strerror(errno));
        abort();
    }

    incoming_client->us_fd.stream.string_data = false;
    incoming_client->us_fd.stream.notify_read = client_read_cb;
    incoming_client->us_fd.stream.notify_state = client_state_cb;
    ustream_fd_init(&incoming_client->us_fd, new_fd);
    add_client(incoming_client);

    LOG_INFO(log_beep_main, "Incoming client connected: %p", incoming_client);
    incoming_client = NULL;
}

int init_relay_server(void)
{
    int fd;
    char port_str[32];

    sprintf(port_str, "%d", server_port);
    fd = usock(USOCK_TCP | USOCK_SERVER | USOCK_IPV4ONLY | USOCK_NUMERIC,
            "0.0.0.0", port_str);
    if(fd < 0)
        return -1;

    // Network performance optimization (this measurably improves performance
    // for Beep). Disable Nagle's algorithm by setting TCP_NODELAY.
    int flag = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
            (char *) &flag, sizeof(int)))  {
        LOG_ERROR(log_beep_main, "setsockopt TCP_NODELAY failed: %s",
                strerror(errno));
        return -1;
    }

    server.cb = server_cb;
    server.fd = fd;
    uloop_fd_add(&server, ULOOP_READ);
    return ubus_register_event_handler_async(ctx, &ev_handler_obj,
            "beep.state.*", NULL, NULL);
}
