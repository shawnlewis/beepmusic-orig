#ifndef _BEEPCOMM_H
#define _BEEPCOMM_H


#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include <libubox/list.h>
#include <libubox/blobmsg.h>
#include <libubox/blobmsg_json.h>
#include <libubox/uloop.h>
#include <libubox/usock.h>
#include <libubox/ustream.h>

#include "beep/config.h"
#include "beep/debug.h"
#include "beep/flags.h"
#include "beep/beep_ubus.h"

#include "mongoose.h"

#define COMM_UBUS_PATH "beep.comm"
#define SSDP_PORT 32334
#define DIAL_PORT 32335
#define MSG_PORT 32336

#define APP_NAME_MAXLEN 128
#define INSTANCE_MAXLEN 8
#define MSG_MAXLEN 65536
#define MSG_HEADER_MAXLEN 8096

#define HELLO_TIMEOUT 5000
#define PING_TIMEOUT 5000

// apps.c
struct apps_context {
    struct mg_context *mg_ctx;
};

struct apps_context *start_apps_api(void);
void stop_apps_api(struct apps_context *apps_ctx);

// header.c

#define HEADER_FIELD_NAME_LEN  128
#define HEADER_FIELD_VALUE_LEN 128

typedef struct _header header_t;

header_t *header_parse(const char *buf);
const char *header_get(header_t *hdr, const char *name);
void header_free(header_t *hdr);

// main.c
struct flag_vals {
    int ssdp_port;
    int dial_port;
    int msg_port;
    bool debug;
    bool ubus_stub;
    bool autostart;
};

extern struct flag_vals comm_flags;

// messages.c
struct messages_context {
    struct ubus_context *ubus_ctx;
    struct mg_context *mg_ctx;
    struct uloop_fd client_event_fd;
    struct list_head client_list;
    pthread_mutex_t client_list_lock;
    bool client_list_locked;

    int n_connections;
};

typedef void (*start_messages_cb_t)(struct messages_context *ctx, void *priv);
typedef void (*stop_messages_cb_t)(void *priv);

void start_messages(struct ubus_context *ubus_ctx,
        start_messages_cb_t cb, void *priv);
void stop_messages(struct messages_context *messages_ctx,
        stop_messages_cb_t cb, void *priv);

// mongoose_util.c
void mgutil_resp_error(struct mg_connection *conn, int status,
        const char *reason);
void mgutil_resp_printf(struct mg_connection *conn, int status,
        const char *desc, const char *type, const char *fmt, ...);
bool mgutil_is_valid(const char *payload, int num_bytes);

// ssdp.c
struct ssdp_context {
    pthread_mutex_t lock;
    char *friendly_name;
    char last_updated[128];
    char model_name[32];
    char uuid[37];

    struct mg_context *mg_ctx;

    struct uloop_fd ufd;
};

struct ssdp_context *start_ssdp(const char *friendly_name,
        const char *model_name, const char *uuid);
void stop_ssdp(struct ssdp_context *ssdp_ctx);

// util.c
char *get_address(const char *host_string);
char *get_socket_name(int fd);
int json_escape_string(const char *src, char *dest, size_t bufsize);
char *long_to_ip_str(long ip);
char *strnstr(const char *haystack, const char *needle, size_t n);
char *strnchr(const char *s, char c, size_t n);
void strtrim(char *in);

typedef void (*session_id_handler_t)(int session_id, void *userdata);
void get_session_id_async(const char *app_name, session_id_handler_t callback,
        void *userdata);

int get_session_id(const char *app_name);

#endif // _BEEPCOMM_H
