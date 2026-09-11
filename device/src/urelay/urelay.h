#ifndef _URELAY_H
#define _URELAY_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>

#include <libubox/blobmsg.h>
#include <libubox/blobmsg_json.h>
#include <libubox/uloop.h>
#include <libubox/usock.h>
#include <libubox/ustream.h>

#include <libubus.h>

#include "beep/debug.h"
#include "beep/urelay.h"
#include "beep/beep_ubus.h"

#define USTREAM_BUF_SIZE 1024 * 32

#define EVENT_PREFIX "beep."
#define EVENT_PREFIX_LENGTH 5

struct client {
    struct list_head head;

    struct sockaddr_in cli_addr;
    struct ustream_fd us_fd;
    bool destroyed;
    int n_refs;
};

extern struct ubus_context *ctx;

extern uint16_t server_port;
extern struct ubus_object relay_object;
extern struct ubus_object_type relay_type;

extern bool debug;

enum {
    BLOB_TYPE_RESPONSE,
    BLOB_TYPE_ERROR,
    BLOB_TYPE_EVENT
};

enum {
    BLOB_RESPONSE_ID,
    BLOB_RESPONSE_MSG,
    _BLOB_RESPONSE_LENGTH
};

enum {
    BLOB_ERROR_ID,
    BLOB_ERROR_CODE,
    BLOB_ERROR_MSG,
    _BLOB_ERROR_LENGTH
};

enum {
    BLOB_INVOKE_ID,
    BLOB_INVOKE_PATH,
    BLOB_INVOKE_METHOD,
    BLOB_INVOKE_MSG,
    _BLOB_INVOKE_LENGTH
};

enum {
    BLOB_EVENT_TYPE,
    BLOB_EVENT_MSG,
    _BLOB_EVENT_LENGTH
};

extern const struct blobmsg_policy response_blob_policy[];
extern const struct blobmsg_policy error_blob_policy[];
extern const struct blobmsg_policy invoke_blob_policy[];
extern const struct blobmsg_policy event_blob_policy[];

extern int TCP_PROBE_COUNT;
extern int TCP_IDLE_TIME;
extern int TCP_INTERVAL;

int init_relay_server(void);

#endif  // _URELAY_H
