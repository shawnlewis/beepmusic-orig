#ifndef BEEP_UBUS_H
#define BEEP_UBUS_H

#include <libubox/blobmsg_json.h>
#include <libubus.h>

#define BEEP_UBUS_EVENT_ID_MAX_LENGTH 1024
#define BEEP_UBUS_DEV_ID_MAX_LENGTH 128

#define BEEP_UBUS_ERROR_DISTRIBUTOR_INVALID_TOKEN 1
#define BEEP_UBUS_ERROR_DISTRIBUTOR_STOPPED 2
#define BEEP_UBUS_ERROR_DISTRIBUTOR_COMMAND_WOULD_BLOCK 3

enum {
    BEEP_UBUS_ERROR = 0,
    /*
     * 1-??? Reserved for UBUS
     */
    BEEP_UBUS_ERROR_ARGS = 10000
};

enum {
    BEEP_RESPONSE_SUCCESS,
    BEEP_RESPONSE_RESULT,
    BEEP_RESPONSE_ERROR_CODE,
    BEEP_RESPONSE_ERROR_MESSAGE,
    __BEEP_RESPONSE_MAX
};

enum {
    BEEP_STATE_STATE,
    BEEP_STATE_EVENT_TYPE,
    BEEP_STATE_EVENT_DATA,
    __BEEP_STATE_MAX
};

enum {
    DISTRIBUTOR_STATE_TRACK_ELAPSED_SECS,
    DISTRIBUTOR_STATE_TOKEN,
    DISTRIBUTOR_STATE_STREAMBUF_USED,
    DISTRIBUTOR_STATE_OUTPUT_USED,
    DISTRIBUTOR_STATE_WRITTEN_TRACK_TIME,
    DISTRIBUTOR_STATE_DURATION,
    DISTRIBUTOR_STATE_LOCAL_VOLUME,
    DISTRIBUTOR_STATE_MASTER_VOLUME,
    DISTRIBUTOR_STATE_STATION,
    DISTRIBUTOR_STATE_TRACK_INFO,
    DISTRIBUTOR_STATE_AUDIO_STATE,
    DISTRIBUTOR_STATE_PLAY_STATE,
    DISTRIBUTOR_STATE_PLAYERS,
    __DISTRIBUTOR_STATE_MAX
};

enum {
    PLAYNET_STATE_NUM_TRACKS_STARTED,
    PLAYNET_STATE_OUTPUT_USED,
    PLAYNET_STATE_OUTPUT_SIZE,
    PLAYNET_STATE_STREAMBUF_FREE,
    PLAYNET_STATE_CAN_ST_BEGIN,
    PLAYNET_STATE_WRITTEN_TRACK_TIME,
    PLAYNET_STATE_SYNC_PLAYED_TIME,
    PLAYNET_STATE_SYNC_TIMESTAMP,
    PLAYNET_STATE_GAIN,
    __PLAYNET_STATE_MAX
};

enum {
    MANAGER_STATE_GROUPS,
    MANAGER_STATE_LOCAL_DEVICE,
    MANAGER_STATE_DEVICES,
    __MANAGER_STATE_MAX
};

enum {
    MANAGER_LOCAL_SOURCE_ID,
    MANAGER_LOCAL_SINK_ID,
    MANAGER_LOCAL_DEVICE_ID,
    MANAGER_LOCAL_SIG_AVG,
    MANAGER_LOCAL_NAME,
    MANAGER_LOCAL_GROUP_LABEL,
    MANAGER_LOCAL_SET_UUID,
    MANAGER_LOCAL_APPS,
    MANAGER_LOCAL_SOURCE_TIME,
    __MANAGER_LOCAL_MAX
};

enum {
    MANAGER_DEVICE_SINK_ID,
    MANAGER_DEVICE_SOURCE_ID,
    MANAGER_DEVICE_NAME,
    MANAGER_DEVICE_PORT,
    MANAGER_DEVICE_IP,
    __MANAGER_DEVICE_MAX
};

#define POLICY(x, y, z) [(x)] = { .name = (y), .type = (z) }

extern const struct blobmsg_policy beep_state_policy[];
extern const struct blobmsg_policy distributor_state_policy[];
extern const struct blobmsg_policy playnet_state_policy[];
extern const struct blobmsg_policy manager_state_policy[];
extern const struct blobmsg_policy manager_local_policy[];
extern const struct blobmsg_policy manager_device_policy[];

void beep_ubus_disconnect(struct ubus_context* ctx);
struct ubus_context* beep_ubus_connect(const char *agent_name);
struct ubus_context* beep_ubus_get_ctx(void);
const char *beep_ubus_get_agent_name(void);

void beep_send_state(
        struct ubus_context* ctx, const char *component,
        const char *event_type, struct blob_attr *event_data,
        struct blob_attr *state);

int beep_ubus_invoke(
        const char* path, const char* method, struct blob_attr* msg,
        struct blob_attr** response);

void beep_ubus_invoke_async(
        const char* path, const char* method, struct blob_attr* msg,
        uint32_t timeout_msecs,
        ubus_invoke_async_handler_t callback, void* priv);

int beep_ubus_send_event(struct ubus_context* ctx,
        const char* id, struct blob_attr* data);

void beep_reply_success(
        struct ubus_context* ctx, struct ubus_request_data* req,
        struct blob_attr* result);

void beep_reply_error(
        struct ubus_context* ctx, struct ubus_request_data* req,
        const char* error_msg, const uint32_t error_code);

bool beep_parse_response(
        const struct blob_attr* response,
        struct blob_attr *parsed[__BEEP_RESPONSE_MAX]);

void beep_host_to_dev_id(char *id, const char *host, int port);

typedef void (*beep_event_handler_t)(const char *component,
        struct blob_attr *state, struct blob_attr *event_data,
        const char *event_type);

struct beep_subscription *beep_ubus_subscribe(
        const char *component, beep_event_handler_t callback,
        void *userdata);

typedef void (*beep_ubus_unsubscribe_complete_handler_t)(void *priv);

void beep_ubus_unsubscribe(struct beep_subscription *s,
        beep_ubus_unsubscribe_complete_handler_t callback,
        void *userdata);

#endif  // BEEP_UBUS_H
