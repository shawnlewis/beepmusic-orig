#include <common.h>

#include <devices.h>

#ifdef CONFIG_NULL_DEVICE
static int null_start(void) {
    return 0;  // ok.
}

static int null_stop(void) {
    return 0;  // ok.
}

static void null_putc(const char c) {
}

static void null_puts(const char *s) {
}

static int null_tstc(void) {
    return 0;  // never any chars ready.
}

static int null_getc(void) {
    return 0;  // should never be called.
}

int drv_null_init(void) {
    device_t dev;
    int rc;

    memset(&dev, 0, sizeof(dev));

    strcpy(dev.name, "null");

    dev.flags = DEV_FLAGS_OUTPUT | DEV_FLAGS_INPUT | DEV_FLAGS_SYSTEM;
    dev.start = null_start;
    dev.stop = null_stop;
    dev.putc = null_putc;
    dev.puts = null_puts;
    dev.tstc = null_tstc;
    dev.getc = null_getc;

    rc = device_register(&dev);

    return (rc == 0) ? 1 : rc;
}
#endif  // CONFIG_NULL_DEVICE
