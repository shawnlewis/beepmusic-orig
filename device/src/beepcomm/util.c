#define _GNU_SOURCE

#include <ctype.h>

#include "beepcomm.h"

/*
 * isolates the hostname/ip address from a <address>:<port> string, or
 * returns NULL if invalid string
 */

#define ADDR_BUF_SZ 128
char *get_address(const char *host_string) {
    static char buf[ADDR_BUF_SZ] = {0,};
    char *colon_ptr = strnchr(host_string, ':', ADDR_BUF_SZ);

    if(!colon_ptr || colon_ptr - host_string > ADDR_BUF_SZ - 1) {
        LOG_ERROR(log_beep_main, "Invalid host string: %s", host_string);
        return NULL;
    }

    memcpy(buf, host_string, colon_ptr - host_string);
    buf[colon_ptr - host_string] = 0;

    return buf;
}

#define SOCK_NAME_BUF_SZ 128
char *get_socket_name(int fd) {
    static char addr_buf[SOCK_NAME_BUF_SZ] = {0,};
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    int ret = getsockname(fd, (struct sockaddr *)&addr, &addr_len);
    if(ret) {
        LOG_ERROR(log_beep_main, "getsockname failed: %s",
                strerror(errno));
        return NULL;
    }

    snprintf(addr_buf, SOCK_NAME_BUF_SZ, "%s:%d", inet_ntoa(addr.sin_addr), addr.sin_port);
    return addr_buf;
}

/*
 * Implemented in accordance with RFC 4627 Sec 2.5 at:
 *
 * https://www.ietf.org/rfc/rfc4627.txt
 */
int json_escape_string(const char *src, char *dest, size_t bufsize) {
    char xbuf[5];
    int si = 0, di = 0;

    while(src[si] && di < bufsize - 1) {
        char c = src[si++];
        if(c == '\\' || c == '"') {
            if(di+3 >= bufsize) {
                break;
            }
            dest[di] = '\\';
            dest[di+1] = c;
            di+=2;
        } else if(c < 0x20) {
            if(di+7 >= bufsize) {
                break;
            }
            snprintf(xbuf, 5, "%04x", c);
            dest[di] = '\\';
            dest[di+1] = 'u';
            memcpy(&dest[di+2], xbuf, 4);
            di+=6;
        } else {
            dest[di++] = c;
        }
    }
    dest[di] = 0;
    return di;
}

/*
 * Convert long to ip address string
 */
char *long_to_ip_str(long ip) {
    static char ip_str[16];
    unsigned char bytes[4] = {
        ip & 0xFF,
        (ip >> 8) & 0xFF,
        (ip >> 16) & 0xFF,
        (ip >> 24) & 0xFF,
    };

    snprintf(ip_str, 16, "%d.%d.%d.%d", bytes[3], bytes[2], bytes[1], bytes[0]);
    return ip_str;
}

char *strnstr(const char *haystack, const char *needle, size_t n) {
    size_t _n = strnlen(haystack, n);
    char *_haystack = alloca(_n+1);

    memcpy(_haystack, haystack, _n);
    _haystack[_n] = 0;
    char *pos = strstr(_haystack, needle);
    return pos ? (char *)(haystack + (pos - _haystack)) : NULL;
}

char *strnchr(const char *s, char c, size_t n) {
    size_t _n = strnlen(s, n);
    char *_s = alloca(_n+1);

    memcpy(_s, s, _n);
    _s[_n] = 0;
    char *pos = strchr(_s, c);
    return pos ? (char *)(s + (pos - _s)) : NULL;
}

/* OPERATES IN PLACE */
// "  12345  "
void strtrim(char *in) {
    int start = 0, len;
    while(isspace(*(in + start))) start++;

    len = strlen(in + start) - 1;

    while(isspace(*(in + start + len))) len--;

    memmove(in, in + start, len + 1);
    in[len + 1] = 0;
}

// ubus_invoke_async_handler_t
struct session_id_obj {
    session_id_handler_t callback;
    void *userdata;
    char *app_name;
};

static void get_session_id_cb(int ubus_ret, struct blob_attr *response, void *priv) {
    struct session_id_obj *obj = (struct session_id_obj *)priv;
    struct blob_attr *result[__BEEP_RESPONSE_MAX];

    if(ubus_ret) {
        LOG_ERROR(log_beep_main, "beep.manager::app_running failed: %s",
                ubus_strerror(ubus_ret));
        obj->callback(-1, obj->userdata);
        goto out;
    }

    if(!beep_parse_response(response, result)) {
        LOG_ERROR(log_beep_main, "Failed to parse beep.manager::app_running response");
        abort();
    }

    if(!result[BEEP_RESPONSE_RESULT]) {
        LOG_WARN(log_beep_main, "Invalid app name '%s'", obj->app_name);
        obj->callback(-1, obj->userdata);
        goto out;
    }

    obj->callback(blobmsg_get_u32(result[BEEP_RESPONSE_RESULT]), obj->userdata);

out:
    free(obj->app_name);
    free(obj);
}

void get_session_id_async(const char *app_name,
        session_id_handler_t callback,
        void *userdata) {
    struct blob_buf *args = calloc(1, sizeof(struct blob_buf));
    struct session_id_obj *obj = calloc(1, sizeof(struct session_id_obj));

    obj->callback = callback;
    obj->userdata = userdata;
    obj->app_name = strdup(app_name);

    blob_buf_init(args, 0);
    blobmsg_add_string(args, "name", app_name);
    beep_ubus_invoke_async("beep.manager", "app_running", args->head,
            5000, &get_session_id_cb, obj);

    blob_buf_free(args);
    free(args);

    return;
}

int get_session_id(const char *app_name) {
    struct blob_attr *response = NULL;
    struct blob_attr *result[__BEEP_RESPONSE_MAX];
    struct blob_buf *args = calloc(1, sizeof(struct blob_buf));
    int ret;
    int running;

    blob_buf_init(args, 0);
    blobmsg_add_string(args, "name", app_name);
    ret = beep_ubus_invoke("beep.manager", "app_running", args->head, &response);

    if(ret) {
        LOG_ERROR(log_beep_main, "beep.manager::app_running failed: %s",
                ubus_strerror(ret));
        exit(0); // This is expected behavior when beep manager has died but
                 // beepcomm has not yet been cleaned up.  No need to emit a
                 // core in this case.

    }

    if(!beep_parse_response(response, result)) {
        LOG_ERROR(log_beep_main, "Failed to parse app_running response");
        abort();
    }

    if(!result[BEEP_RESPONSE_RESULT]) {
        LOG_WARN(log_beep_main, "Invalid app name '%s'", app_name);
        running = -1;
        goto out;
    }

    running = blobmsg_get_u32(result[BEEP_RESPONSE_RESULT]);

out:
    free(response);
    blob_buf_free(args);
    free(args);
    return running;
}
