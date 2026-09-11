#ifndef BEEPCLOUD_H
#define BEEPCLOUD_H

#include <curl/curl.h>

#include "beep/beep_uloop.h"
#include "beep/beep_ubus.h"
#include "beep/debug.h"

#define TIMEOUT_MS (10000)

#define GROUP_ID_MAX_LEN (128)
#define URL_PREFIX_MAX_LEN (256)

#define URL_MAX_LEN (URL_PREFIX_MAX_LEN + 64)
#define DATA_MAX_LEN (256)

#define DEFAULT_CLUSTER_ID "0"
#define CLUSTER_ID_MAX_LEN (128)

#define DEFAULT_HOST "reverb.beepdevices.com"
#define HOST_MAX_LEN (128)

#define DEFAULT_PATH "/1/data/"
#define PATH_MAX_LEN (128)

#define DEFAULT_PORT "40937"
#define PORT_MAX_LEN (128)

#define RAW_KEY_MAX_LEN (256)

#define KEY_MAX_LEN (64)

typedef void (*get_callback_t)(
        const char *key,
        const char *value,
        size_t len,
        void *userdata);

typedef void (*set_callback_t)(
        const char *key,
        bool ok,
        void *userdata);

typedef void (*delete_callback_t)(
        const char *key,
        bool ok,
        void *userdata);

void data_get(const char *key, get_callback_t done, void *userdata);
void data_set(const char *key, const char *value, size_t len,
        set_callback_t done, void *userdata);
void data_delete(const char *key, delete_callback_t done, void *userdata);

extern char *esc_cluster_id;
extern char url_prefix[];
extern CURLSH *curl_share;

#endif

