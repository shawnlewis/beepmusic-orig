#ifndef UBUS_TRACE_H
#define UBUS_TRACE_H

volatile sig_atomic_t trace_on;

#define UBUS_TRACE(...) \
    if (trace_on) \
        fprintf (stderr, "UBUS_TRACE: " __VA_ARGS__)

void ubus_trace_init();

#endif
