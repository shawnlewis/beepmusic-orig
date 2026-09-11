#include <arpa/inet.h>
#include <unistd.h>
#include <libubox/blobmsg_json.h>
#include <libubox/uloop.h>
#include <libubox/usock.h>
#include <libubox/list.h>
#include <libubox/ustream.h>
#include <libubus.h>
#include <dns_sd.h>
#include <netdb.h>
//#include <netinet/in.h>

#include "beep/beep_ubus.h"
#include "beep/beeplib.h"
#include "beep/config.h"
#include "beep/debug.h"
#include "beep/flags.h"
#include "beep/urelay.h"
#include "beep/beep_http.h"

#define BEEPHEAD_SIMPLE_PORT 32201

#define COMMAND_PAUSE 0
#define COMMAND_RESUME 1
#define COMMAND_ADJUST_VOLUME 2
#define COMMAND_SKIP 3

#define UPDATE_AUDIO_STATE 0
#define UPDATE_VOLUME 1

#define AUDIO_STATE_PLAYING 0
#define AUDIO_STATE_PAUSED 1
#define AUDIO_STATE_WORKING 2
#define AUDIO_STATE_SHUFFLING 3



///// Flags

struct flag_vals {
    int control_port;
    int uhttpd_port;
};

// With default values;
struct flag_vals beepdiscovery_flags = {
    .control_port = URELAY_PORT,
    .uhttpd_port = BEEP_HTTP_PORT
};

static const BeepFlag flags[] = {
    BEEP_FLAG("control_port", BEEP_FLAG_INT, &beepdiscovery_flags.control_port, NULL, NULL),
    BEEP_FLAG("uhttpd_port", BEEP_FLAG_INT, &beepdiscovery_flags.uhttpd_port, NULL, NULL)
};

BEEP_INIT_FLAGS(flags);

///// End Flags


static struct ubus_context *ctx;
static struct ubus_event_handler listener;

static struct uloop_fd server;
static struct ustream_fd client_ustreamfd;
bool connected = false;

char* current_audio_state;
int current_volume;

void send_message(int update, int arg) {
    uint8_t buf[8];
    if (connected) {
        printf("Sending update: %d %d\n", update, arg);
        net_writeint(buf, update);
        net_writeint(buf+4, arg);
        ustream_write(&client_ustreamfd.stream, (char*) buf, 8, false);
    }
}

void send_current_audio_state(void) {
    printf("Sending audio state\n");
    if (!strcmp(current_audio_state, "playing")) {
        send_message(UPDATE_AUDIO_STATE, AUDIO_STATE_PLAYING);
    } else if (!strcmp(current_audio_state, "paused")) {
        send_message(UPDATE_AUDIO_STATE, AUDIO_STATE_PAUSED);
    } else if (!strcmp(current_audio_state, "working")) {
        send_message(UPDATE_AUDIO_STATE, AUDIO_STATE_WORKING);
    } else if (!strcmp(current_audio_state, "shuffling")) {
        send_message(UPDATE_AUDIO_STATE, AUDIO_STATE_SHUFFLING);
    }
}

void send_current_volume(void) {
    //printf("Sending volume\n");
    send_message(UPDATE_VOLUME, current_volume);
}

static void client_read_cb(struct ustream *s, int bytes)
{
    do {
        int len;
        char* buf = ustream_get_read_buf(s, &len);
        if (len < 8) {
            return;
        }
        int command = net_readint((uint8_t*) buf);
        int arg = net_readint((uint8_t*) buf+4);
        ustream_consume(s, 8);

        printf("GOT command %d, arg %d\n", command, arg);

        struct blob_attr* response;
        char *method;
        static struct blob_buf args;
        blob_buf_init(&args, 0);

        switch (command) {
            case COMMAND_PAUSE:
                method = "pause";
                break;
            case COMMAND_RESUME:
                method = "smart_resume";
                break;
            case COMMAND_SKIP:
                method = "skip";
                break;
            case COMMAND_ADJUST_VOLUME:
                method = "adjust_local_volume";
                blobmsg_add_u32(&args, "volume", arg);
                break;
            default:
                LOG_ERROR(log_beep_main, "Received invalid command %d\n",
                        command);
                return;
        }

        beep_ubus_invoke("beep.distributor", method, args.head, &response);
    } while(1);
}

static void client_state_cb(struct ustream *s)
{
    // Only interested in EOF
    if(!s->eof)
        return;

    if(!s->w.data_bytes) {
        LOG_INFO(log_beep_main, "Connection closed (EOF)");
        connected = false;
        close(client_ustreamfd.fd.fd);
    }
}

static void server_cb(struct uloop_fd *ufd, unsigned int events) {
    printf("Server cb\n");
    struct sockaddr_in addr;
    unsigned int len = sizeof(struct sockaddr_in);
    int new_fd = accept(server.fd, (struct sockaddr *) &addr, &len);
    if(new_fd < 0) {
        LOG_ERROR(log_beep_main, "Failed to accept connection: %s",
                strerror(errno));
        return;
    }

    memset(&client_ustreamfd, 0, sizeof(struct ustream_fd));
    client_ustreamfd.stream.string_data = false;
    client_ustreamfd.stream.notify_read = client_read_cb;
    client_ustreamfd.stream.notify_state = client_state_cb;
    connected = true;
    ustream_fd_init(&client_ustreamfd, new_fd);

    LOG_INFO(log_beep_main, "Got client");
    usleep(100);
    send_current_audio_state();
    send_current_volume();
}

void handle_update(struct blob_attr* msg, const char* event_type) {

    struct blob_attr *state[__DISTRIBUTOR_STATE_MAX];
    blobmsg_parse(distributor_state_policy, __DISTRIBUTOR_STATE_MAX,
            state, blobmsg_data(msg), blob_len(msg));

    if (!state[DISTRIBUTOR_STATE_AUDIO_STATE]) {
        LOG_ERROR(log_beep_main, "Received invalid distributor state");
        return;
    }
    const char* audio_state = blobmsg_get_string(
            state[DISTRIBUTOR_STATE_AUDIO_STATE]);
    if (!current_audio_state || strcmp(current_audio_state, audio_state)) {
        free(current_audio_state);
        current_audio_state = strdup(audio_state);
        send_current_audio_state();
    }

    int volume = blobmsg_get_u32(state[DISTRIBUTOR_STATE_LOCAL_VOLUME]);
    if (current_volume != volume) {
        current_volume = volume;
        send_current_volume();
    }
}

void on_distributor_event(
        struct ubus_context *ctx, struct ubus_event_handler *ev,
        const char* type, struct blob_attr* msg) {
    struct blob_attr *tb[__BEEP_STATE_MAX];
    blobmsg_parse(beep_state_policy, __BEEP_STATE_MAX,
            tb, blob_data(msg), blob_len(msg));
    handle_update(tb[BEEP_STATE_STATE],
            blobmsg_get_string(tb[BEEP_STATE_EVENT_TYPE]));
}

int main(int argc, char *argv[])
{
    log_beep_main = LOG_CATEGORY_GET("beephead_simple");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    beep_flags_init(argc, argv);

    uloop_init();

    ctx = beep_ubus_connect("beephead_simple");
    if(!ctx) {
        LOG_ERROR(log_beep_main, "Failed to connect to ubus");
        return 1;
    }

    ubus_add_uloop(ctx);

    char port_str[32];
    sprintf(port_str, "%d", BEEPHEAD_SIMPLE_PORT);
    int fd = usock(USOCK_TCP | USOCK_SERVER | USOCK_IPV4ONLY | USOCK_NUMERIC,
            "0.0.0.0", port_str);
    if(fd < 0)
        return -1;

    server.cb = server_cb;
    server.fd = fd;
    uloop_fd_add(&server, ULOOP_READ);

    // subscribe to events
    struct blob_attr* response;
    beep_ubus_invoke("beep.distributor", "get_state", NULL, &response);
    struct blob_attr *parsed[__BEEP_RESPONSE_MAX];
    if (!beep_parse_response(response, parsed)) {
        printf("get_state failed\n");
        exit(1);
    }
    handle_update(parsed[BEEP_RESPONSE_RESULT], NULL);
    free(response);

    // racy, we could miss an event
    listener.cb = on_distributor_event;
    ubus_register_event_handler(
            ctx, &listener, "beep.state.distributor._local_");

    uloop_run();

    uloop_done();
    return 0;
}
