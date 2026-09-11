#include <asm/addrspace.h>
#include <common.h>
#include "ar7240_soc.h"


// WileE D1
#define CONFIG_BEEP_GPIO_LED_FACT_TEST_PASS_BIT     16

void beep_fact_test_exit(int status) {
    // Only turn on the LED if the factory test passes.
    if (!status) {
        ar7240_reg_wr(AR7240_GPIO_OUT,
                (ar7240_reg_rd(AR7240_GPIO_OUT)
                | (1 << CONFIG_BEEP_GPIO_LED_FACT_TEST_PASS_BIT)));
        ar7240_reg_wr(AR7240_GPIO_OE,
                (ar7240_reg_rd(AR7240_GPIO_OE)
                | (1 << CONFIG_BEEP_GPIO_LED_FACT_TEST_PASS_BIT)));
    }
}
