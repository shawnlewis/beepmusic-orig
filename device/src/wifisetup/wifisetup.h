#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

#include <stdint.h>
#include <stdbool.h>


typedef enum {
    OP_MODE_UNKNOWN = 0,
    OP_MODE_STA,
    OP_MODE_AP,
    OP_MODE_MONITOR
} OPMode;

#define CONN_RETURN_ON_UNKNOWN                      (0)
#define CONN_RETURN_ON_ALWAYS                       (1<<0)
#define CONN_RETURN_ON_NEVER                        (1<<1)
#define CONN_RETURN_ON_CONNECT_ERROR                (1<<2)
#define CONN_RETURN_ON_CONFIRM_ERROR                (1<<3)

#define ENC_TYPE_UNKNOWN                            (0)
#define ENC_TYPE_NONE                               (1<<0)
#define ENC_TYPE_WEP                                (1<<1)
#define ENC_TYPE_WPA                                (1<<2)
#define ENC_TYPE_WPA2                               (1<<3)

typedef struct AP AP;

struct AP {
    char *essid;
    uint8_t enc_type;
    uint8_t bssid[6];
    int8_t signal;
    uint8_t channel;
    uint8_t miss_counter;
    AP *next;
};

typedef enum {
    BE_MODE_UNKNOWN = 0,
    BE_MODE_CLIENT,
    BE_MODE_AP_SETUP
} BEMode;


typedef enum {
    WIFI_SETUP_CMD_NONE = 0,  // Don't use.
    WIFI_SETUP_CMD_DAEMON
} WifiSetupCmd;

#define CONNECT_TIMEOUT_COUNT                       (3)
#define CONFIG_DEVICE_ID_SIZE                       (32)
#define CONFIG_DEVICE_NAME_SIZE                     (64)

typedef struct {
    char *ifname;
    char device_id[CONFIG_DEVICE_ID_SIZE];
    char device_name[CONFIG_DEVICE_NAME_SIZE];
    WifiSetupCmd cmd;
    int conn_timeout[CONNECT_TIMEOUT_COUNT];
    bool need_ubus;
} WifiSetupConfig;

extern WifiSetupConfig *ws_config;
extern struct ubus_context *ubus_ctx;

void ws_config_cleanup(void);
int ws_config_init(int argc, char **argv);
void ws_ubus_cleanup(void);
int ws_ubus_init(void);

void ws_ap_head_free(AP *ap_head);
AP *ws_ap_copy(const AP *ap);

const char *bssidstr(const uint8_t* bssid);
// Value of bssid is undefined if this returns non-zero.
int strtobssid(const char *str, uint8_t *bssid);
const char *opmodestr(OPMode opmode);  // Not thread safe.
const char *encstr(uint8_t enc_type);
uint8_t strtoenc(const char *str);
uint8_t strtoconnret(const char *str);
void ws_shutdown_evt(void);


void ws_netbe_cleanup(void);
int ws_netbe_init(void);
OPMode ws_netbe_op_mode(void);
AP *ws_netbe_scan(void);  // Will block.
int ws_netbe_set_mode(BEMode be_mode);
// The caller of this must check reason to see if the connection was
// successful or not.  The return value of this function is only if
// the it completed successfully.  If this returns non-zero the values
// int reason and reason_str are undefined.  The caller is responsible
// for freeing reason_str.
int ws_netbe_connect(const AP *ap, const char *key, uint8_t return_on,
        int *reason, char **reason_str);  // Will block.
int ws_netbe_commit(void);
int ws_netbe_revert(void);


int wsd_start(void);


#endif  // WIFI_SETUP_H
