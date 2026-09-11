#include <stdio.h>

#include "beep/debug.h"

#include "wifisetup.h"


static int cmd_daemon(void) {
    return wsd_start();
}

int main(int argc, char **argv) {
    int ret;

    log_beep_main = LOG_CATEGORY_GET("wifisetup");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    if ((ret = ws_config_init(argc, argv)))
        goto done;

    LOG_DEBUG(log_beep_main, "ifname: %s cmd: %d", ws_config->ifname,
            ws_config->cmd);

    if ((ret = ws_ubus_init()))
        goto done;

    if ((ret = ws_netbe_init()))
        goto done;

    switch (ws_config->cmd) {
    case WIFI_SETUP_CMD_DAEMON:
        ret = cmd_daemon();
        break;
    default:
        LOG_ERROR(log_beep_main, "unknown command");
    }

done:
    ws_netbe_cleanup();
    ws_ubus_cleanup();
    ws_config_cleanup();

    return ret;
}
