#include <fcntl.h>
#include <linux/input.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "beep/beep_ubus.h"
#include "wifisetup.h"

#define BSSID_STR_SIZE                              (18)

#define WS_SHUTDOWN_EVT_DELAY                       (6000 * 1000)
// the gpio-polled-keys driver only allows events that have been registered
// with the kernel.  Use the same key as the wifi-setup button.
#define WS_SHUTDOWN_KEY                             (KEY_WPS_BUTTON)
#define WS_EVT_SIZE                                 (sizeof(struct input_event) * 2)

struct str_key_pair {
    const char *str;
    uint8_t enc_type;
};

// Keys should always define 0 as unknown/error.
static uint8_t strtokey(const char *str, const struct str_key_pair *tb, int count) {
    int i;
    if (!str)
        return 0;
    for (i = 0; i < count; i++) {
        if (!strcmp(str, tb[i].str)) {
            return tb[i].enc_type;
        }
    }
    return 0;
}

void ws_ap_head_free(AP *ap_head) {
    AP *next_ap;
    while (ap_head) {
        next_ap = ap_head->next;
        if (ap_head->essid)
            free(ap_head->essid);
        free(ap_head);
        ap_head = next_ap;
    }
}

AP *ws_ap_copy(const AP *ap) {
    AP *new_ap = calloc(1, sizeof(AP));
    memcpy(new_ap, ap, sizeof(AP));

    if(ap->essid) {
        new_ap->essid = strdup(ap->essid);
    }

    return new_ap;
}

const char *bssidstr(const uint8_t* bssid) {
    static const char hex_char[] = "0123456789abcdef";
    static char str[BSSID_STR_SIZE] = "00:00:00:00:00:00";

    int i;

    for (i = 0; i < 6; i++) {
        str[i * 3] = hex_char[bssid[i] >> 4];
        str[(i * 3) + 1] = hex_char[bssid[i] & 0xf];
    }

    return str;
}

int strtobssid(const char *str, uint8_t *bssid) {
    int i;
    int j;
    char c;

    if (strlen(str) != 17)
        return 1;

    for (i = 0; i < 6; i++) {
        j = 1;
        bssid[i] = 0;

        do {
            c = str[(i * 3) + 1 - j];
            bssid[i] <<= 4;
            if (c >= '0' && c <= '9')
                bssid[i] += c - '0';
            else if (c >= 'a' && c <= 'f')
                bssid[i] += c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                bssid[i] += c - 'A' + 10;
            else
                return 1;
        } while (j--);
    }

    return 0;
}

const char *opmodestr(OPMode opmode) {
    switch (opmode) {
    case OP_MODE_STA:
        return "sta";
    case OP_MODE_AP:
        return "ap";
    case OP_MODE_MONITOR:
        return "monitor";
    case OP_MODE_UNKNOWN:
    default:
        return "unknown";
    }
}

static const struct str_key_pair enc_type_tb[] = {
    {"none",        ENC_TYPE_NONE},
    {"wep",         ENC_TYPE_WEP},
    {"wpa",         ENC_TYPE_WPA},
    {"wpa2",        ENC_TYPE_WPA2},
    {"wpa mixed",   (ENC_TYPE_WPA | ENC_TYPE_WPA2)}
};
#define ENC_TYPE_COUNT (sizeof(enc_type_tb)/sizeof(struct str_key_pair))

const char *encstr(uint8_t enc_type) {
    switch (enc_type) {
    case ENC_TYPE_NONE:
        return enc_type_tb[0].str;
    case ENC_TYPE_WEP:
        return enc_type_tb[1].str;
    case ENC_TYPE_WPA:
        return enc_type_tb[2].str;
    case ENC_TYPE_WPA2:
        return enc_type_tb[3].str;
    case ENC_TYPE_WPA | ENC_TYPE_WPA2:
        return enc_type_tb[4].str;
    case ENC_TYPE_UNKNOWN:
    default:
        return "unknown";
    }
}

uint8_t strtoenc(const char *str) {
    return strtokey(str, enc_type_tb, ENC_TYPE_COUNT);
}

static const struct str_key_pair conn_return_tb[] = {
    {"always",          CONN_RETURN_ON_ALWAYS},
    {"never",           CONN_RETURN_ON_NEVER},
    {"connect_error",   CONN_RETURN_ON_CONNECT_ERROR},
    {"confirm_error",   CONN_RETURN_ON_CONFIRM_ERROR}
};
#define CONN_RETURN_COUNT (sizeof(conn_return_tb)/sizeof(struct str_key_pair))

uint8_t strtoconnret(const char *str) {
    return strtokey(str, conn_return_tb, CONN_RETURN_COUNT);
}

void ws_shutdown_evt(void) {
#ifdef BEEP_DEVICE
    struct blob_attr* response = NULL;
    beep_ubus_invoke("beep.mainio", "disable_setup", NULL, &response);
    if (response)
        free(response);
#endif
}
