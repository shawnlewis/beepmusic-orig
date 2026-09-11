#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#include "ubus_trace.h"

volatile sig_atomic_t trace_on = 0;

static void handle_usr1 (int sig) {
    if (!trace_on) {
        trace_on = !trace_on;
        UBUS_TRACE("trace started\n");
    } else {
        UBUS_TRACE("trace stopped\n");
        trace_on = !trace_on;
    }
}

void ubus_trace_init() {
    struct sigaction usr_action;
    sigset_t block_mask;

    sigfillset(&block_mask);
    usr_action.sa_handler = handle_usr1;
    usr_action.sa_mask = block_mask;
    usr_action.sa_flags = 0;
    sigaction(SIGUSR1, &usr_action, NULL);
}
