#include <asm/addrspace.h>
#include <common.h>
#include "ar7240_soc.h"

// Use this as an offset so it should be valid no matter which addressing mode
// we use.
#define OFFSET_UNDEFINED            (0xffffffff)

#define BC_SECTOR_BASE              (CFG_ENV_ADDR)
#define BC_SECTOR_END               (CFG_ENV_ADDR + CFG_ENV_SECT_SIZE - 1)
#define BC_BASE                     (CFG_ENV_ADDR + CFG_ENV_SIZE)
#define BC_END                      (BC_SECTOR_END)
#define BC_MAX_OFFSET               (CFG_ENV_SECT_SIZE - CFG_ENV_SIZE - 1)

static ulong bootcount_offset = OFFSET_UNDEFINED;

static int set_bootcount(uchar v) {
    uchar *p = (uchar *)(BC_BASE + bootcount_offset);
    int rc = 0;

    if (bootcount_offset == OFFSET_UNDEFINED
            || bootcount_offset > BC_MAX_OFFSET) {
        return -1;
    }

    // Write to the shadow copy.
    *p = v;

    // Commit to flash.
    if (flash_sect_protect(0, BC_SECTOR_BASE, BC_SECTOR_END)
            || (rc = flash_write((char *)p, (ulong)p, 1))) {
        if (rc)
            flash_perror(rc);
    }
    flash_sect_protect(1, BC_SECTOR_BASE, BC_SECTOR_END);

    return rc;
}

int get_bootcount(void) {
    uchar *p = (uchar *)BC_BASE;
    uchar v;

    if (bootcount_offset != OFFSET_UNDEFINED) {
        p = (uchar *)(BC_BASE + bootcount_offset);
    }

    while (p <= (uchar *)BC_END) {
        if (*p) {
            bootcount_offset = (ulong)p - BC_BASE;
            v = *p;
            // Figure out if we should use the first or second nibble.
            if ((v & 0xf0) != 0)
                v >>= 4;

            switch (v) {
            case 0xf:
                return 0;
            case 0x7:
                return 1;
            case 0x3:
                return 2;
            case 0x1:
                return 3;
            default:
                set_bootcount(0);
                // Reset the offset on errors to ensure all corner cases are
                // covered.
                bootcount_offset = OFFSET_UNDEFINED;
                return -2;
            }
        }
        p++;
    }

    bootcount_offset = OFFSET_UNDEFINED;
    return -1;
}

void inc_bootcount(void) {
    uchar *p;
    int bc;
    uchar v;

    // Error check and make sure bootcount_offset is correct.
    bc = get_bootcount();
    if (bc < 0)
        return;

    p = (uchar *)(BC_BASE + bootcount_offset);
    v = *p;

    if ((v & 0xf0) != 0) {
        // Do not increment past 3 boots.
        if (v == 0x1f)
            return;
        v = ((v >> 5) << 4) | 0xf;
        set_bootcount(v);
    } else {
        // Do not increment past 3 boots.
        if (v == 0x01)
            return;
        v >>= 1;
        set_bootcount(v);
    }
}

#ifdef GPIO_TEST_MODE_BIT
int test_mode_gpio_status(void) {
    unsigned int gpio;

    gpio = ar7240_reg_rd(AR7240_GPIO_IN);

    if (gpio & (1 << GPIO_TEST_MODE_BIT)) {
#ifdef GPIO_TEST_MODE_IS_ACTIVE_LOW
        return 0;
#else
        return 1;
#endif
    } else {
#ifdef GPIO_TEST_MODE_IS_ACTIVE_LOW
        return 1;
#else
        return 0;
#endif
    }
}

void set_test_mode_ok_led(int enable) {
    if (enable) {
        ar7240_reg_wr(AR7240_GPIO_OUT,
                (ar7240_reg_rd(AR7240_GPIO_OUT)
                | (1 << GPIO_TEST_MODE_OK_LED_BIT)));
        ar7240_reg_wr(AR7240_GPIO_OE,
                (ar7240_reg_rd(AR7240_GPIO_OE)
                | (1 << GPIO_TEST_MODE_OK_LED_BIT)));
    } else {
        ar7240_reg_wr(AR7240_GPIO_OE,
                (ar7240_reg_rd(AR7240_GPIO_OE)
                & ~(1 << GPIO_TEST_MODE_OK_LED_BIT)));
        ar7240_reg_wr(AR7240_GPIO_OUT,
                (ar7240_reg_rd(AR7240_GPIO_OUT)
                & ~(1 << GPIO_TEST_MODE_OK_LED_BIT)));
    }
}
#endif  // GPIO_TEST_MODE_BIT
