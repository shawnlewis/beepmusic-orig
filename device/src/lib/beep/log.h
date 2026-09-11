#ifndef BEEP_LOG_H
#define BEEP_LOG_H

#include <stdbool.h>

// Only use these macros for logging so switching architectures is easier.

// Define LOG_GLOBAL_CATEGORY before include "log.h" to use a single log
// category and omit it from the LOG_* macros.  You must still init the
// category before using it.

// log_category should be initialized in the main thread.

// Comment these to enable LOG_TEST and LOG_TRACE.
#define NLOG_TEST
#define NLOG_TRACE

#define LOG_CATEGORY struct log_category
#define LOG_CATEGORY_GET(NAME) log_category_get(NAME)
#define LOG_CATEGORY_SET_PRIORITY(LCAT, PRI) ((LCAT)->pri = PRI)
#define LOG_SET_STDERR_PRIORITY(PRI) log_global_stderr_priority = PRI
#define LOG_SET_SYSLOG_PRIORITY(PRI) log_global_syslog_priority = PRI
#define IS_LOG_PRIORITY(LCAT, PRI) ((LCAT)->pri >= PRI)
#define LOG_CLEANUP() log_cleanup()

// Use this to directly log to syslog using ident instead of using
// something like logger.  Use NULL to disable direct syslog, only
// if enabled.
#define LOG_SET_SYSLOG_IDENT(IDENT) log_set_syslog_ident(IDENT)

#ifdef USE_PRETTY_FUNCTION
#define LOG_FUNC __PRETTY_FUNCTION__
#else  // USE_PRETTY_FUNCTION
#define LOG_FUNC __func__
#endif  // USE_PRETTY_FUNCTION

// Do not define these macros in log.c since they overlap with syslog.h
#ifndef __IN_LOG_C

#ifndef NLPRINTF
#define LPRINTF(LCAT, PRI, FMT, ...) \
    do { \
        if ((LCAT)->pri >= PRI) \
            lprintf(LCAT, PRI, LOG_FUNC, __LINE__, FMT, ##__VA_ARGS__); \
    } while(0)
#else  // NLPRINTF
#define LPRINTF(PRI, FMT, ...)
#endif  // NLPRINTF

#ifndef NLOG_ERROR
#ifdef LOG_GLOBAL_CATEGORY
#define LOG_ERROR(FMT, ...) \
    LPRINTF(LOG_GLOBAL_CATEGORY, LOG_PRIORITY_ERROR, FMT, ##__VA_ARGS__)
#else  // !LOG_GLOBAL_CATEGORY
#define LOG_ERROR(LCAT, FMT, ...) \
    LPRINTF(LCAT, LOG_PRIORITY_ERROR, FMT, ##__VA_ARGS__)
#endif  // LOG_GLOBAL_CATEGORY
#else  // NLOG_ERROR
#define LOG_ERROR(FMT, ...)
#endif  // NLOG_ERROR

#ifndef NLOG_WARN
#ifdef LOG_GLOBAL_CATEGORY
#define LOG_WARN(FMT, ...) \
    LPRINTF(LOG_GLOBAL_CATEGORY, LOG_PRIORITY_WARN, FMT, ##__VA_ARGS__)
#else  // !LOG_GLOBAL_CATEGORY
#define LOG_WARN(LCAT, FMT, ...) \
    LPRINTF(LCAT, LOG_PRIORITY_WARN, FMT, ##__VA_ARGS__)
#endif  // LOG_GLOBAL_CATEGORY
#else  // NLOG_WARN
#define LOG_WARN(FMT, ...)
#endif  // NLOG_WARN

#ifndef NLOG_INFO
#ifdef LOG_GLOBAL_CATEGORY
#define LOG_INFO(FMT, ...) \
    LPRINTF(LOG_GLOBAL_CATEGORY, LOG_PRIORITY_INFO, FMT, ##__VA_ARGS__)
#else  // !LOG_GLOBAL_CATEGORY
#define LOG_INFO(LCAT, FMT, ...) \
    LPRINTF(LCAT, LOG_PRIORITY_INFO, FMT, ##__VA_ARGS__)
#endif  // LOG_GLOBAL_CATEGORY
#else  // NLOG_INFO
#define LOG_INFO(FMT, ...)
#endif  // NLOG_INFO

#ifndef NLOG_DEBUG
#ifdef LOG_GLOBAL_CATEGORY
#define LOG_DEBUG(FMT, ...) \
    LPRINTF(LOG_GLOBAL_CATEGORY, LOG_PRIORITY_DEBUG, FMT, ##__VA_ARGS__)
#else  // !LOG_GLOBAL_CATEGORY
#define LOG_DEBUG(LCAT, FMT, ...) \
    LPRINTF(LCAT, LOG_PRIORITY_DEBUG, FMT, ##__VA_ARGS__)
#endif  // LOG_GLOBAL_CATEGORY
#else  // NLOG_DEBUG
#define LOG_DEBUG(FMT, ...)
#endif  // NLOG_DEBUG

#ifndef NLOG_TEST
#ifdef LOG_GLOBAL_CATEGORY
#define LOG_TEST(FMT, ...) \
    LPRINTF(LOG_GLOBAL_CATEGORY, LOG_PRIORITY_TEST, FMT, ##__VA_ARGS__)
#else  // !LOG_GLOBAL_CATEGORY
#define LOG_TEST(LCAT, FMT, ...) \
    LPRINTF(LCAT, LOG_PRIORITY_TEST, FMT, ##__VA_ARGS__)
#endif  // LOG_GLOBAL_CATEGORY
#else  // NLOG_TEST
#define LOG_TEST(FMT, ...)
#endif  // NLOG_TEST

#ifndef NLOG_TRACE
#ifdef LOG_GLOBAL_CATEGORY
#define LOG_TRACE(FMT, ...) \
    LPRINTF(LOG_GLOBAL_CATEGORY, LOG_PRIORITY_TRACE, FMT, ##__VA_ARGS__)
#else  // !LOG_GLOBAL_CATEGORY
#define LOG_TRACE(LCAT, FMT, ...) \
    LPRINTF(LCAT, LOG_PRIORITY_TRACE, FMT, ##__VA_ARGS__)
#endif  // LOG_GLOBAL_CATEGORY
#else  // NLOG_TRACE
#define LOG_TRACE(FMT, ...)
#endif  // NLOG_TRACE

#endif  // __IN_LOG_C


typedef enum {
    LOG_PRIORITY_TEST = -1,
    LOG_PRIORITY_OFF,
    LOG_PRIORITY_ERROR,
    LOG_PRIORITY_WARN,
    LOG_PRIORITY_INFO,
    LOG_PRIORITY_DEBUG,
    LOG_PRIORITY_TRACE
} LogPriority;

struct log_category {
    struct log_category *next;
    LogPriority pri;
    char name[0];
};

extern LogPriority log_global_stderr_priority;
extern LogPriority log_global_syslog_priority;

// Catch all logging category that should always point back to main.
extern struct log_category *log_beep_main;

void log_set_syslog_ident(const char *ident);
void log_cleanup(void);
struct log_category *log_category_get(const char *name);
// Should deprecate this.
void log_category_set_priority(struct log_category *lcat, LogPriority pri);
void lprintf(struct log_category *lcat, LogPriority pri, const char *func,
        int line, const char *fmt, ...)
        __attribute__((format (printf, 5, 6)));


#endif  // BEEP_LOG_H
