#include <asm/addrspace.h>
#include <common.h>
#include "ar7240_soc.h"

#define CONFIG_BEEP_GPIO_LED_NW64_RED_BIT           26
#define CONFIG_BEEP_GPIO_LED_NW64_GREEN_BIT         27
#define CONFIG_BEEP_GPIO_LED_NW64_BLUE_BIT          8

void beep_target_init(void) {
    // We could disable JTAG here to enable use of the blue LED (GPIO8) but
    // we would also lose JTAG pretty early in the u-boot init process.

    // Set red LED during uboot init.
    ar7240_reg_wr(AR7240_GPIO_OUT,
            (ar7240_reg_rd(AR7240_GPIO_OUT)
            | (1 << CONFIG_BEEP_GPIO_LED_NW64_RED_BIT)));
    ar7240_reg_wr(AR7240_GPIO_OE,
            (ar7240_reg_rd(AR7240_GPIO_OE)
            | (1 << CONFIG_BEEP_GPIO_LED_NW64_RED_BIT)));
}

void beep_bootb_preboot(void) {
    // Set green LED right before booting the kernel (yellow/orange).
    ar7240_reg_wr(AR7240_GPIO_OUT,
            (ar7240_reg_rd(AR7240_GPIO_OUT)
            | (1 << CONFIG_BEEP_GPIO_LED_NW64_GREEN_BIT)));
    ar7240_reg_wr(AR7240_GPIO_OE,
            (ar7240_reg_rd(AR7240_GPIO_OE)
            | (1 << CONFIG_BEEP_GPIO_LED_NW64_GREEN_BIT)));
}
