#include <common.h>
#include <command.h>
#include <asm/mipsregs.h>
#include <asm/addrspace.h>
#include <config.h>
#include <version.h>
#include "ar7240_soc.h"


#define AR7240_WATCHDOG_TMR_CONTROL_LAST_RESET_MASK (0x80000000)
#define AR7240_WATCHDOG_TMR_CONTROL_ACTION_MASK     (0x00000003)
#define AR7240_WATCHDOG_TMR_CONTROL_ACTION_NONE     (0)
#define AR7240_WATCHDOG_TMR_CONTROL_ACTION_GPI      (1)
#define AR7240_WATCHDOG_TMR_CONTROL_ACTION_NMI      (2)
#define AR7240_WATCHDOG_TMR_CONTROL_ACTION_FCR      (3)


extern void ar7240_ddr_initial_config(uint32_t refresh);
extern int ar7240_ddr_find_size(void);

static GPIOInfo board_gpio_btn[] = {
#ifdef CONFIG_BEEP_GPIO_BTN_SETUP_BIT
    {
        BEEP_GPIO_BTN_SETUP_ID,
        CONFIG_BEEP_GPIO_BTN_SETUP_BIT,
        CONFIG_BEEP_GPIO_BTN_SETUP_POLARITY
    },
#endif  // CONFIG_BEEP_GPIO_BTN_SETUP_BIT
#ifdef CONFIG_BEEP_GPIO_BTN_FACT_TEST_BIT
    {
        BEEP_GPIO_BTN_FACT_TEST_ID,
        CONFIG_BEEP_GPIO_BTN_FACT_TEST_BIT,
        CONFIG_BEEP_GPIO_BTN_FACT_TEST_POLARITY
    },
#endif  // CONFIG_BEEP_GPIO_BTN_FACT_TEST_BIT
};

static int board_gpio_btn_count = sizeof(board_gpio_btn)/sizeof(GPIOInfo);

static void ar7240_usb_initial_config(void) {
#ifndef CONFIG_HORNET_EMU
    ar7240_reg_wr_nf(AR7240_USB_PLL_CONFIG, 0x0a04081e);
    ar7240_reg_wr_nf(AR7240_USB_PLL_CONFIG, 0x0804081e);
#endif
}

static void ar7240_usb_otp_config(void) {
    unsigned int addr, reg_val, reg_usb;
    int time_out, status, usb_valid = 0;

    for (addr = 0xb8114014; ;addr -= 0x10) {
        status = 0;
        time_out = 20;

        reg_val = ar7240_reg_rd(addr);

        while ((time_out > 0) && (~status)) {
            if ((( ar7240_reg_rd(0xb8115f18)) & 0x7) == 0x4) {
                status = 1;
            } else {
                status = 0;
            }
            time_out--;
        }

        reg_val = ar7240_reg_rd(0xb8115f1c);
        if ((reg_val & 0x80) == 0x80){
            usb_valid = 1;
            reg_usb = reg_val & 0x000000ff;
        }

        if (addr == 0xb8114004) {
            break;
        }
    }

    if (usb_valid) {
        reg_val = ar7240_reg_rd(0xb8116c88);
        reg_val &= ~0x03f00000;
        reg_val |= (reg_usb & 0xf) << 22;
        ar7240_reg_wr(0xb8116c88, reg_val);
    }
}

static void ar7240_gpio_config(void) {
    // Ensure GPIO_FUNC is in the reset state, except for serial.
    ar7240_reg_wr(AR7240_GPIO_FUNC,
            (ar7240_reg_rd(AR7240_GPIO_FUNC) & 0x00048002));

    // Same for OE
    ar7240_reg_wr(AR7240_GPIO_OE,
            (ar7240_reg_rd(AR7240_GPIO_OE) & 0x00000400));

    // Enable GPIO26/GPIO27 control in BOOT_STRAP
    ar7240_reg_wr(HORNET_BOOTSTRAP_STATUS,
            (ar7240_reg_rd(HORNET_BOOTSTRAP_STATUS) | 0x00040000));
}

int button_read(int button_id){
    unsigned int gpio;
    int i;

    for (i = 0; i < board_gpio_btn_count; i++) {
        if (button_id == board_gpio_btn[i].id) {
            gpio = ar7240_reg_rd(AR7240_GPIO_IN)
                    & (1 << board_gpio_btn[i].bit);
            if (board_gpio_btn[i].polarity == BEEP_GPIO_ACTIVE_HIGH) {
                return !!gpio;
            } else {
                return !gpio;
            }
        }
    }

    // Invalid id.
    return -1;
}


#ifdef CONFIG_SHOW_ACTIVITY
void show_activity(int arg) {
}
#endif


#ifdef CONFIG_SHOW_BOOT_PROGRESS
void show_boot_progress(int arg) {
}
#endif


#ifdef CONFIG_BEEP_BOOTB_WATCHDOG_ENABLE
void board_wdt_enable(void) {
    // Watchdog timer is sourced from AHB until AR934x when it is sourced
    // from ref.
    // With a AHB of 200MHz this gives about 21s to boot the kernel and
    // reset the counter.
    ar7240_reg_wr(AR7240_WATCHDOG_TMR, 0xffffffff);
    // Flush write.
    ar7240_reg_rd(AR7240_WATCHDOG_TMR);


    // From the kernel driver (ath79_wdt.c).
    // Updating the TIMER register requires a few microseconds
    // on the AR934x SoCs at least. Use a small delay to ensure
    // that the TIMER register is updated within the hardware
    // before enabling the watchdog.
    udelay(2);

    ar7240_reg_wr(AR7240_WATCHDOG_TMR_CONTROL,
            (ar7240_reg_rd(AR7240_WATCHDOG_TMR_CONTROL) &
            AR7240_WATCHDOG_TMR_CONTROL_LAST_RESET_MASK)
            | AR7240_WATCHDOG_TMR_CONTROL_ACTION_FCR);
    // Flush write.
    ar7240_reg_rd(AR7240_WATCHDOG_TMR_CONTROL);
}

void board_wdt_disable(void) {
    ar7240_reg_wr(AR7240_WATCHDOG_TMR_CONTROL,
            (ar7240_reg_rd(AR7240_WATCHDOG_TMR_CONTROL) &
            AR7240_WATCHDOG_TMR_CONTROL_LAST_RESET_MASK)
            | AR7240_WATCHDOG_TMR_CONTROL_ACTION_NONE);
    // Flush write.
    ar7240_reg_rd(AR7240_WATCHDOG_TMR_CONTROL);
}
#endif  // CONFIG_BEEP_BOOTB_WATCHDOG_ENABLE


static int ar7240_mem_config(void) {
#ifndef COMPRESSED_UBOOT
    unsigned int tap_val1 __attribute__((unused));
    unsigned int tap_val2 __attribute__((unused));
#endif

    ar7240_gpio_config();	// init GPIO

    ar7240_ddr_initial_config(CFG_DDR_REFRESH_VAL);

/* Default tap values for starting the tap_init*/
    ar7240_reg_wr (AR7240_DDR_TAP_CONTROL0, CFG_DDR_TAP0_VAL);
    ar7240_reg_wr (AR7240_DDR_TAP_CONTROL1, CFG_DDR_TAP1_VAL);

#ifndef COMPRESSED_UBOOT
    ar7240_ddr_tap_init();

    tap_val1 = ar7240_reg_rd(0xb800001c);
    tap_val2 = ar7240_reg_rd(0xb8000020);
    debug("#### TAP VALUE 1 = %x, 2 = %x\n",tap_val1, tap_val2);
#endif

    ar7240_usb_initial_config();
    ar7240_usb_otp_config();
    //hornet_ddr_tap_init();

    BEEP_TARGET_INIT();

    return (ar7240_ddr_find_size());
}

long int initdram(int board_type) {
    return (ar7240_mem_config());
}

#ifdef COMPRESSED_UBOOT
int checkboard (char *board_string)
{
    strcpy(board_string, "Beep "CONFIG_BEEP_BOARD_STRING" board");
    return 0;
}
#else
int checkboard (void)
{
    puts("Beep "CONFIG_BEEP_BOARD_STRING" board\n\n");
    return 0;
}
#endif /* #ifdef COMPRESSED_UBOOT */
