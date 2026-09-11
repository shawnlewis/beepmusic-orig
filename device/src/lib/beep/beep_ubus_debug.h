#ifndef BEEP_UBUS_DEBUG_H
#define BEEP_UBUS_DEBUG_H

#include "beep_ubus.h"

int beep_ubus_debug_start(const struct ubus_method *methods, size_t n_method,
        const char* name);
void beep_ubus_debug_stop(void);

#endif  // BEEP_UBUS_DEBUG_H
