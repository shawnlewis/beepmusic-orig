#include <stdio.h>

#include "net_flags.h"

// With default values;
struct flag_vals net_flags;

static const BeepFlag flags[] = {
    BEEP_FLAG("network_delay", BEEP_FLAG_INT, &net_flags.network_delay, NULL, NULL)
};

BEEP_INIT_FLAGS(flags);
