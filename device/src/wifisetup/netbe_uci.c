#include <stdlib.h>
#include <string.h>
#include <uci.h>

#include "beep/debug.h"
#include "beep/flags.h"

#include "wifisetup.h"
#include "netbe_ctrl.h"


struct package_state {
    char *pkg_name;
    struct uci_ptr ptr;
};

enum {
    PKG_WIRELESS = 0,
    PKG_STATE_SIZE
};

static struct uci_context *uci_ctx;
static struct package_state pkg_state[PKG_STATE_SIZE];


void ws_netbe_ctrl_cleanup(void) {
    int i;

    for (i = 0; i < PKG_STATE_SIZE; i++) {
        if (pkg_state[i].pkg_name) {
            free(pkg_state[i].pkg_name);
        }
    }

    if (uci_ctx) {
        uci_free_context(uci_ctx);
        uci_ctx = NULL;
    }
}

int ws_netbe_ctrl_init(void) {
    const BeepFlag *uci_flag = beep_flags_get("uciconfig");
    const char *uci_dir = NULL;
    int i;
    int ret;

    if (uci_flag)
        uci_dir = *((char **)(uci_flag->val));

    if (!uci_dir)
        return 1;

    uci_ctx = uci_alloc_context();
    if (!uci_ctx)
        return 1;

    ret = uci_set_confdir(uci_ctx, uci_dir);
    if (ret != UCI_OK) {
        LOG_ERROR(log_beep_main, "could not set confdir %s: %d", uci_dir, ret);
        return 1;
    }

    // Setup names.
    pkg_state[PKG_WIRELESS].pkg_name = strdup("wireless");

    // Get top level pointers.
    for (i = 0; i < PKG_STATE_SIZE && ret == UCI_OK; i++) {
        ret = uci_lookup_ptr(uci_ctx, &pkg_state[i].ptr,
                pkg_state[i].pkg_name, true);
        if (ret != UCI_OK) {
            LOG_ERROR(log_beep_main, "could not get %s ptr: %d",
                    pkg_state[i].pkg_name, ret);
        }
    }

    return 0;
}

int ws_netbe_ctrl_net_restart(void) {
    int ret = system("/etc/init.d/network restart");
    // Not sure why but this occasionally returns an exit status of 255 even
    // though it doesn't fail.  Ignore just this error code.
    if (ret != -1 && ret != 0) {
        LOG_ERROR(log_beep_main, "error restarting networking: %d", ret);
        LOG_ERROR(log_beep_main, "WIFSIGNALED: %d WEXITSTATUS: %d",
            WIFSIGNALED(ret), WEXITSTATUS(ret));
        return 1;
    }
    return 0;
}

static int ws_uci_modify(char *str, bool delete) {
    struct uci_ptr ptr;
    int ret;

    ret = uci_lookup_ptr(uci_ctx, &ptr, str, true);
    if (ret != UCI_OK) {
        LOG_ERROR(log_beep_main, "could not get ptr: %d", ret);
    }

    if (ret == UCI_OK) {
        if (delete) {
            ret = uci_delete(uci_ctx, &ptr);
            // Ignore not found error on delete.
            if (ret == UCI_ERR_NOTFOUND) {
                ret = UCI_OK;
            }
        } else {
            ret = uci_set(uci_ctx, &ptr);
        }

        if (ret != UCI_OK) {
            LOG_ERROR(log_beep_main, "could not %s value: %d",
                    delete ? "delete" : "set", ret);
        }
    }

    if (ret == UCI_OK) {
        ret = uci_save(uci_ctx, ptr.p);
        if (ret != UCI_OK) {
            LOG_ERROR(log_beep_main, "could not save value: %d", ret);
        }
    }

    return ret;
}

static int ws_uci_set(char *str) {
    return ws_uci_modify(str, false);
}

//static int ws_uci_delete(char *str) {
//    return ws_uci_modify(str, true);
//}

static const char *mode_strs[] = {
    "setup",
    "client"
};
#define MODE_COUNT (sizeof(mode_strs)/sizeof(char *))

BEMode ws_netbe_ctrl_get_mode(void) {
    int modes = 0;
    int i;

    for (i = 0; i < MODE_COUNT; i++) {
        char *str;
        struct uci_ptr ptr;
        int ret;

        if (asprintf(&str, "wireless.%s_mode.disabled", mode_strs[i]) == -1) {
            return BE_MODE_UNKNOWN;
        }

        ret = uci_lookup_ptr(uci_ctx, &ptr, str, true);
        if (ret != UCI_OK
                || ptr.o->type != UCI_TYPE_STRING
                || !ptr.o->v.string) {
            LOG_ERROR(log_beep_main, "could not get value: %d", ret);
            free(str);
            return BE_MODE_UNKNOWN;
        }

        modes <<= 1;
        modes |= *ptr.o->v.string == '0' ? 0 : 1;

        free(str);
    }

    // setup, client (reverse from set_mode).
    switch (modes) {
    case 0x1:
        return BE_MODE_AP_SETUP;
    case 0x2:
        return BE_MODE_CLIENT;
    default:
        break;
    }
    return BE_MODE_UNKNOWN;
}

int ws_netbe_ctrl_set_mode(BEMode be_mode) {
    int modes = 0;
    int i;

    // client, setup (reverse from get_mode).
    switch (be_mode) {
    case BE_MODE_AP_SETUP:
        modes = 0x2;
        break;
    case BE_MODE_CLIENT:
        modes = 0x1;
        break;
    default:
        LOG_ERROR(log_beep_main, "unknown be_mode: %d", be_mode);
        return 1;
    }

    for (i = 0; i < MODE_COUNT; i++) {
        char *str;
        int ret;

        if (asprintf(&str, "wireless.%s_mode.disabled=%d", mode_strs[i],
            (modes & 0x1)) == -1) {
            return 1;
        }
        modes >>= 1;

        ret = ws_uci_set(str);

        free(str);

        if (ret != UCI_OK) {
            return 1;
        }
    }

    return 0;
}

int ws_netbe_ctrl_set_client_ap(const AP *ap, const char *key) {
    static const char *options[] = {
        "ssid",
        "encryption",
        "key"
    };
    const char *values[3];
    int i;

    if (!ap || !ap->essid)
        return 1;

    values[0] = ap->essid;

    if (ap->enc_type == ENC_TYPE_NONE) {
        values[1] = "none";
        values[2] = NULL;
    } else {
        if (!key) {
            LOG_ERROR(log_beep_main, "missing key");
            return 1;
        }
        values[2] = key;

        switch (ap->enc_type) {
        case ENC_TYPE_WEP:
            values[1] = "wep";
            break;

        case ENC_TYPE_WPA:
            values[1] = "psk";
            break;

        case ENC_TYPE_WPA2:
        case ENC_TYPE_WPA | ENC_TYPE_WPA2:
            // For mixed mode default to WPA2.
            values[1] = "psk2";
            break;

        default:
            LOG_ERROR(log_beep_main, "unknown enc_type");
            return 1;
        }
    }

    for (i = 0; i < sizeof(options)/sizeof(char *); i++) {
        char *str;
        int ret;
        // NULL values are optional so delete them.
        bool delete = values[i] ? false : true;

        if (delete) {
            if (asprintf(&str, "wireless.client_mode.%s", options[i])
                    == -1) {
                return 1;
            }
        } else {
            if (asprintf(&str, "wireless.client_mode.%s=%s", options[i],
                values[i]) == -1) {
                return 1;
            }
        }

        ret = ws_uci_modify(str, delete);

        free(str);

        if (ret != UCI_OK) {
            return 1;
        }
    }

    return 0;
}

int ws_netbe_ctrl_commit(void) {
    int i;
    int ret;

    for (i = 0; i < PKG_STATE_SIZE; i++) {
        LOG_INFO(log_beep_main, "saving package: %s", pkg_state[i].pkg_name);
        ret = uci_commit(uci_ctx, &pkg_state[i].ptr.p, true);
        if (ret) {
            LOG_ERROR(log_beep_main, "saving package: %d", ret);
            return ret;
        }
    }
    return 0;
}

int ws_netbe_ctrl_revert(void) {
    int i;
    int ret;

    for (i = 0; i < PKG_STATE_SIZE; i++) {
        LOG_INFO(log_beep_main, "reverting package: %s", pkg_state[i].pkg_name);
        ret = uci_revert(uci_ctx, &pkg_state[i].ptr);
        if (ret) {
            LOG_ERROR(log_beep_main, "reverting package: %d", ret);
            return ret;
        }
    }
    return 0;
}
