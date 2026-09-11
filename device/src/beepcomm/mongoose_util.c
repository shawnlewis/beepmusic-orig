#include "beepcomm.h"

void mgutil_resp_error(struct mg_connection *conn, int status,
        const char *reason) {
    char buf[4096] = {0,};
    int len = 0;

    if(status > 199 && status != 204 && status != 304) {
        len = snprintf(buf, sizeof(buf), "Error %d: %s\r\n", status, reason);
    }
    
    mg_printf(conn, "HTTP/1.1 %d %s\r\n" \
            "Content-Type: text/plain\r\n" \
            "Content-Length: %d\r\n" \
            "\r\n" \
            "%s", status, reason, len, buf);
}

void mgutil_resp_printf(struct mg_connection *conn, int status,
        const char *desc, const char *type, const char *fmt, ...) {
    char buf[4096] = {0,};
    int len = 0;

    va_list args;
    va_start(args, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    mg_printf(conn, "HTTP/1.1 %d %s\r\n" \
            "Content-Type: %s\r\n" \
            "Content-Length: %d\r\n" \
            "\r\n" \
            "%s", status, desc,
            type ? type : "text/plain",
            len,
            buf);
}

bool mgutil_is_valid(const char *payload, int num_bytes) {
    int i;
    for(i = 0; i < num_bytes; i++) {
        if(((payload[i] & 0x80) == 0x80) ||
                (payload[i] == 0x7F) ||
                (payload[i] <= 0x1F))
            return false;
    }
    return true;
}
