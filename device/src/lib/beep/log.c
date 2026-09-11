#define _GNU_SOURCE
#include <alloca.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

#define __IN_LOG_C
#include "log.h"

#define NEW_CATEGORY_DEFAULT_PRIORITY               LOG_PRIORITY_INFO

// Comment this out to use malloc and unlimited log buffer size.
#define LOG_BUFFER_ALLOCA_SIZE                      (640)

static bool syslog_enabled;
static struct log_category *lcat_head;

LogPriority log_global_stderr_priority = LOG_PRIORITY_DEBUG;
LogPriority log_global_syslog_priority = LOG_PRIORITY_OFF;
struct log_category *log_beep_main;


void log_set_syslog_ident(const char *ident) {
    // First call wins until disabled.
    if (ident && syslog_enabled)
        return;

    if (syslog_enabled) {
        closelog();
        syslog_enabled = false;
    }

    if (ident) {
        openlog(ident, LOG_ODELAY | LOG_CONS, LOG_USER);
        syslog_enabled = true;
    }
}

void log_cleanup(void) {
    struct log_category *next_lcat;

    log_set_syslog_ident(NULL);

    while (lcat_head) {
        next_lcat = lcat_head->next;
        free(lcat_head);
        lcat_head = next_lcat;
    }
}

struct log_category *log_category_get(const char *name) {
    struct log_category *ptr = lcat_head;

    while (ptr) {
        if (!strcmp(name, ptr->name))
            return ptr;
        ptr = ptr->next;
    }

    ptr = (struct log_category *)malloc(sizeof(struct log_category)
            + strlen(name) + 1);

    ptr->next = lcat_head;
    lcat_head = ptr;
    ptr->pri = NEW_CATEGORY_DEFAULT_PRIORITY;
    strcpy(ptr->name, name);
    return ptr;
}

// Should deprecate this.
void log_category_set_priority(struct log_category *lcat, LogPriority pri) {
    lcat->pri = pri;
}

void lprintf(struct log_category *lcat, LogPriority pri, const char *func,
        int line, const char *fmt, ...) {
    struct timespec now;
    struct tm tm;
    va_list ap;
    uint64_t millis;
    const char *color;
    const char *pri_name;
#ifdef LOG_BUFFER_ALLOCA_SIZE
    char *dec_msg = alloca(LOG_BUFFER_ALLOCA_SIZE);
    int bytes = 0;
#else
    char *msg;
    char *dec_msg;
    int ret;
#endif
    size_t dec_msg_len;

    if (log_global_stderr_priority < pri
            && (!syslog_enabled || log_global_syslog_priority < pri)) {
        return;
    }

    clock_gettime(CLOCK_REALTIME, &now);
    gmtime_r(&now.tv_sec, &tm);
    millis = ((uint64_t) now.tv_sec * 1000ULL) +
            ((uint64_t) now.tv_nsec / 1000000ULL);

    switch (pri) {
    case LOG_PRIORITY_TEST:
        color = "\033[1;35m";  // magenta bold
        pri_name = "TEST";
        break;

    case LOG_PRIORITY_ERROR:
        color = "\033[0;31m";  // red
        pri_name = "ERROR";
        break;
    case LOG_PRIORITY_WARN:
        color = "\033[0;32m";  // green
        pri_name = "WARN";
        break;
    case LOG_PRIORITY_INFO:
        color = "\033[0;33m";  // yellow
        pri_name = "INFO";
        break;
    case LOG_PRIORITY_DEBUG:
        color = "\033[0;34m";  // blue
        pri_name = "DEBUG";
        break;
    default:
    case LOG_PRIORITY_TRACE:
        color = "\033[0;36m";  // cyan
        pri_name = "TRACE";
        break;
    }

#ifdef LOG_BUFFER_ALLOCA_SIZE
    // Reserve 5 bytes for the color reset chars and newline.
    bytes = snprintf(dec_msg,
            LOG_BUFFER_ALLOCA_SIZE - 5,
            "%s%04d%02d%02d %02d:%02d:%02d.%03ld %" PRId64 " %-6s %s - ",
            color,
            tm.tm_year + 1900,
            tm.tm_mon + 1,
            tm.tm_mday,
            tm.tm_hour,
            tm.tm_min,
            tm.tm_sec,
            (long)(now.tv_nsec / 1000000ULL),
            millis,
            pri_name,
            lcat->name);

    // When checking if there is any space left reserve one extra byte for the
    // NULL terminating char that snprintf doesn't count in its return value.
    if (func && bytes < LOG_BUFFER_ALLOCA_SIZE - 6) {
        bytes += snprintf(dec_msg + bytes,
                LOG_BUFFER_ALLOCA_SIZE - 5 - bytes,
                "%s:%d ",
                func,
                line);
    }

    if (bytes < LOG_BUFFER_ALLOCA_SIZE - 6) {
        va_start(ap, fmt);
        bytes += vsnprintf(dec_msg + bytes,
                LOG_BUFFER_ALLOCA_SIZE - 5 - bytes,
                fmt,
                ap);
        va_end(ap);
    }

    // (v)snprintf will return the number of bytes it could have written, if
    // so reset back to the end of the buffer for the color reset chars.
    if (bytes > LOG_BUFFER_ALLOCA_SIZE - 6) {
        bytes = LOG_BUFFER_ALLOCA_SIZE - 6;
    }

    dec_msg[bytes] = '\033';
    dec_msg[bytes + 1] = '[';
    dec_msg[bytes + 2] = '0';
    dec_msg[bytes + 3] = 'm';
    dec_msg[bytes + 4] = '\n';
    dec_msg[bytes + 5] = '\0';
#else
    va_start(ap, fmt);
    ret = vasprintf(&msg, fmt, ap);
    va_end(ap);
    if (ret == -1) {
        return;
    }

    // Allow func/line to be optional for callers who include it with fmt.
    if (func) {
        ret = asprintf(&dec_msg,
                "%s%04d%02d%02d %02d:%02d:%02d.%03ld %" PRId64 " %-6s %s - %s:%d %s\033[0m\n",
                color,
                tm.tm_year + 1900,
                tm.tm_mon + 1,
                tm.tm_mday,
                tm.tm_hour,
                tm.tm_min,
                tm.tm_sec,
                (long)(now.tv_nsec / 1000000ULL),
                millis,
                pri_name,
                lcat->name,
                func,
                line,
                msg);
    } else {
        ret = asprintf(&dec_msg,
                "%s%04d%02d%02d %02d:%02d:%02d.%03ld %" PRId64 " %-6s %s - %s\033[0m\n",
                color,
                tm.tm_year + 1900,
                tm.tm_mon + 1,
                tm.tm_mday,
                tm.tm_hour,
                tm.tm_min,
                tm.tm_sec,
                (long)(now.tv_nsec / 1000000ULL),
                millis,
                pri_name,
                lcat->name,
                msg);
    }

    free(msg);

    if (ret == -1) {
        return;
    }
#endif

    if (log_global_stderr_priority >= pri) {
        fputs(dec_msg, stderr);
    }

    if (syslog_enabled && log_global_syslog_priority >= pri) {
        dec_msg_len = strlen(dec_msg);
        // Trim color codes for syslog.
        dec_msg[dec_msg_len - 5] = '\0';
        syslog(LOG_NOTICE, "%s", dec_msg + strlen(color));
    }

#ifndef LOG_BUFFER_ALLOCA_SIZE
    free(dec_msg);
#endif
}
