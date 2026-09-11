#include <assert.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>

#include "beep/beep_ubus.h"
#include "beep/config.h"
#include "beep/debug.h"
#include "beep/flags.h"

#include "wifisetup.h"


#define WIFI_SETUP_DEFAULT_T0                       (3000)
#define WIFI_SETUP_DEFAULT_T1                       (1000)
#define WIFI_SETUP_DEFAULT_T2                       (5000)
#define WIFI_SETUP_DEFAULT_IFNAME                   "wlan0"
#define WIFI_SETUP_DEFAULT_CMD                      (WIFI_SETUP_CMD_DAEMON)

struct cmd_string_pair {
    const char *name;
    WifiSetupCmd cmd;
};

static const struct cmd_string_pair cmd_strings[] = {
    {"daemon", WIFI_SETUP_CMD_DAEMON}
};

static BeepFlag flags[] = {
    BEEP_FLAG("ifname", BEEP_FLAG_STRING, NULL, "DEV",
            "network device name"),
    BEEP_FLAG("t0", BEEP_FLAG_INT, NULL, "MSEC",
            "first event timeout (evt 19)"),
    BEEP_FLAG("t1", BEEP_FLAG_INT, NULL, "MSEC",
            "t1 to connect timeout (evt 46)"),
    BEEP_FLAG("t2", BEEP_FLAG_INT, NULL, "MSEC",
            "t2 to error timeout (evt 20)")
};

WifiSetupConfig *ws_config;
struct ubus_context *ubus_ctx;


void ws_config_cleanup(void) {
    if (ws_config) {
        if (ws_config->ifname)
            free(ws_config->ifname);
        free(ws_config);
    }
}

int ws_config_init(int argc, char **argv) {
    int i;

    if (ws_config) {
        LOG_WARN(log_beep_main, "called more than once");
        return 1;
    }

    ws_config = (WifiSetupConfig *)malloc(sizeof(WifiSetupConfig));
    assert(ws_config);
    memset(ws_config, 0, sizeof(WifiSetupConfig));

    ws_config->conn_timeout[0] = WIFI_SETUP_DEFAULT_T0;
    ws_config->conn_timeout[1] = WIFI_SETUP_DEFAULT_T1;
    ws_config->conn_timeout[2] = WIFI_SETUP_DEFAULT_T2;

    flags[0].val = &ws_config->ifname;
    flags[1].val = &ws_config->conn_timeout[0];
    flags[2].val = &ws_config->conn_timeout[1];
    flags[3].val = &ws_config->conn_timeout[2];

    beep_flags_init_with_flags(argc, argv, flags,
            sizeof(flags)/sizeof(BeepFlag));

    if (optind < argc) {
        for (i = 0; i < sizeof(struct cmd_string_pair)/sizeof(cmd_strings);
                i++) {
            if (!strcmp(cmd_strings[i].name, argv[optind])) {
                ws_config->cmd = cmd_strings[i].cmd;
                break;
            }
            LOG_ERROR(log_beep_main, "unknown cmd: %s", argv[optind]);
            exit(1);
        }
    }

    // Setup defaults for anything not specified.
    if (!ws_config->ifname) {
        ws_config->ifname = strdup(WIFI_SETUP_DEFAULT_IFNAME);
    }

    if (ws_config->cmd == WIFI_SETUP_CMD_NONE) {
        ws_config->cmd = WIFI_SETUP_DEFAULT_CMD;
    }

    // Don't need ubus for everything so don't always connect.
    switch (ws_config->cmd) {
    case WIFI_SETUP_CMD_DAEMON:
        ws_config->need_ubus = true;
        break;
    default:
        break;
    }

    // Get device-id from uci.
    if (beep_config_device_read("device_id", ws_config->device_id,
            CONFIG_DEVICE_ID_SIZE) == -1) {
        strcpy(ws_config->device_id, "beep-unknown");
    }

    // Get device name from uci
    if (beep_config_data_read("device_name", ws_config->device_name,
                CONFIG_DEVICE_NAME_SIZE) == -1) {
        strcpy(ws_config->device_name, "Unnamed Beep");
    }

    return 0;
}

void ws_ubus_cleanup(void) {
    if (ubus_ctx) {
        uloop_fd_delete(&ubus_ctx->sock);
        beep_ubus_disconnect(ubus_ctx);
        uloop_done();
    }
    ubus_ctx = NULL;
}

int ws_ubus_init(void) {
    if (!ws_config->need_ubus)
        return 0;

    uloop_init();
    ubus_ctx = beep_ubus_connect("wifisetup");
    if (ubus_ctx) {
        ubus_add_uloop(ubus_ctx);
    } else {
        LOG_ERROR(log_beep_main, "could not connect to ubus");
    }
    return ubus_ctx ? 0 : 1;
}
