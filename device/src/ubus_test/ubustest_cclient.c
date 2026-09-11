#include <arpa/inet.h>
#include <unistd.h>
#include <libubox/blobmsg_json.h>
#include <libubox/uloop.h>
#include <libubox/usock.h>
#include <libubox/list.h>
#include <libubox/ustream.h>
#include <libubus.h>
#include <dns_sd.h>
#include <netdb.h>

#include "beep/beep_ubus.h"
#include "beep/beeplib.h"
#include "beep/config.h"
#include "beep/debug.h"
#include "beep/flags.h"


static struct ubus_context *ctx;

void send_data(uint8_t* ptr, size_t size) {
    struct blob_buf* args = calloc(1, sizeof(struct blob_buf));
    blob_buf_init(args, 0);

    // Need to null terminate because ubus checks that there is a \0 on the
    // end, and does nothing if it doesn't find it.
    char* data_terminated = malloc(size + 1);
    memcpy(data_terminated, ptr, size);
    data_terminated[size] = '\0';
    if(blobmsg_add_field(args, BLOBMSG_TYPE_STRING, "data",
              data_terminated, size + 1) == -1) {
        LOG_ERROR(log_beep_main, "Error adding data field");
        abort();
    }
    free(data_terminated);

    struct blob_attr* response;
    int ret = beep_ubus_invoke(
            "ubustest_luaserver", "handle_data", args->head, &response);

    if (ret) {
        LOG_ERROR(log_beep_main, "ubus error: %s", ubus_strerror(ret));
    }

    free(args);
    free(response);
}

int main(int argc, char *argv[])
{
    log_beep_main = LOG_CATEGORY_GET("beephead_simple");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    beep_flags_init(argc, argv);

    uloop_init();

    ctx = beep_ubus_connect("beephead_simple");
    if(!ctx) {
        LOG_ERROR(log_beep_main, "Failed to connect to ubus");
        return 1;
    }

    ubus_add_uloop(ctx);

    uint8_t* data = malloc(16 * 1024);

    fprintf(stderr, "sending data\n");

    uint64_t start_millis = beep_millis();
    for (int i=0; i<1000; i++) {
        send_data(data, 16 * 1024);
    }

    int delta = beep_millis() - start_millis;
    int avg_duration = delta / 1000;
    fprintf(stderr, "Average invoke duration: %d. total time for 1000: %d\n", avg_duration, delta);

    return 0;
}
