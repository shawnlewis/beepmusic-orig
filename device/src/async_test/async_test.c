#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <libubox/uloop.h>

#include "beep/beep_uloop.h"
#include "beep/debug.h"

static void async_task(void *userdata) {
    char *str = (char *)userdata;
    printf("async_task received %s\n", str);
    printf("WAITING 1s\n");
    sleep(1);
    printf("WOKE UP\n");
    strcat(str, ", added string");
}

static void async_done(void *userdata) {
    char *str = (char *)userdata;
    printf("async_done received %s\n", str);
    free(str);
}

int main(int argc, char **argv) {
    log_beep_main = LOG_CATEGORY_GET("async_test");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    uloop_init();

    /*
     * This loop creates 26 async tasks, passing a 64-byte string initialized
     * to a single letter, A-Z.
     */
    for(int x=0;x<26;x++) {
        char *str = (char *)malloc(64);
        memset(str,0,64);
        str[0] = 65+x;
        str[1] = 0;
        beep_async_task(&async_task, &async_done, str);
    }
    uloop_run();
}
