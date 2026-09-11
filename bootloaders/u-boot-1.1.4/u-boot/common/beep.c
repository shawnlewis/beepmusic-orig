#include <asm/addrspace.h>
#include <common.h>
#include <command.h>

// Use this as an offset so it should be valid no matter which addressing mode
// we use.
#define OFFSET_UNDEFINED                            (0xffffffff)

#define BC_SECTOR_BASE                              (CFG_ENV_ADDR)
#define BC_SECTOR_END                               (CFG_ENV_ADDR + CFG_ENV_SECT_SIZE - 1)
#define BC_BASE                                     (CFG_ENV_ADDR + CFG_ENV_SIZE)
#define BC_END                                      (BC_SECTOR_END)
#define BC_MAX_OFFSET                               (CFG_ENV_SECT_SIZE - CFG_ENV_SIZE - 1)

#define MAX_BOOTCOUNT                               (3)
// Setup button must be held down for 5s.
#define BOOT_RECOVERY_SETUP_BUTTON_MS_DELAY         (500)
#define BOOT_RECOVERY_SETUP_BUTTON_MAX_COUNT        (10)

// common/cmd_mem.c
int do_mem_mtest (cmd_tbl_t *cmdtp, int flag, int argc, char *argv[]);
// common/cmd_bootm.c
int do_bootm (cmd_tbl_t *cmdtp, int flag, int argc, char *argv[]);

// inc_bootcount will always get the bootcount before setting it.  Store the
// bootcount offset here so we don't have to search for it again.
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

    printf("Set bootcount 0x%02x offset 0x%08x (%d)\n\n", v, bootcount_offset, rc);
    return rc;
}

// Returns:
// 0-3: Number of boots before reset by kernel.
// -1: No empty space left.
// -2: Invalid value found (this value will be cleared).
static int get_bootcount(void) {
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

static void inc_bootcount(void) {
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

// Run some u-boot based test, turn on the test ok LED if everything is
// fine and boot normally.  OpenWrt level software will also detect the
// test mode gpio and start more tests.
int do_fact_test(cmd_tbl_t * cmdtp, int flag, int argc, char *argv[]) {
    char *cmd_argv[5] = {};
    int ret;

    // Call target specific hooks.
    BEEP_FACT_TEST_ENTER();

    // Call mtest.
    cmd_argv[0] = "mtest";
    cmd_argv[1] = CONFIG_BEEP_FACT_TEST_MEM_START;
    cmd_argv[2] = CONFIG_BEEP_FACT_TEST_MEM_END;
    cmd_argv[3] = "2";  // max iterations
    ret = do_mem_mtest(cmdtp, 0, 4, cmd_argv);
    if (ret) {
        printf("mtest failed with %d\n", ret);
    }

    // Call target specific hooks.
    BEEP_FACT_TEST_EXIT(ret);

    return ret;
}

U_BOOT_CMD(facttest, 1, 0, do_fact_test,
        "facttest- Beep factory test\n",
        "Beep factory test\n"
);

int do_bootb(cmd_tbl_t * cmdtp, int flag, int argc, char *argv[]) {
    char *cmd_argv[3] = {};
#ifdef CONFIG_BEEP_BOOTB_WATCHDOG_ENABLE
    char *watchdog_env = NULL;
#endif  // CONFIG_BEEP_BOOTB_WATCHDOG_ENABLE
    int bc = get_bootcount();
    int boot_primary = (bc < MAX_BOOTCOUNT);

#ifdef CONFIG_BEEP_GPIO_BTN_SETUP_BIT
    int counter = 0;

    if (boot_primary && (button_read(BEEP_GPIO_BTN_SETUP_ID) == 1)) {
        while ((button_read(BEEP_GPIO_BTN_SETUP_ID) == 1)
                && counter < BOOT_RECOVERY_SETUP_BUTTON_MAX_COUNT) {
            udelay(BOOT_RECOVERY_SETUP_BUTTON_MS_DELAY * 1000);
            counter++;
        }
    }

    if (counter >= BOOT_RECOVERY_SETUP_BUTTON_MAX_COUNT) {
        boot_primary = 0;
    }
#endif  // CONFIG_BEEP_GPIO_BTN_SETUP_BIT

#ifdef CONFIG_BEEP_GPIO_BTN_FACT_TEST_BIT
    if (button_read(BEEP_GPIO_BTN_FACT_TEST_ID) == 1) {
        int ret;

        cmd_argv[0] = "facttest";
        cmd_argv[1] = NULL;
        ret = do_fact_test(cmdtp, 0, 1, cmd_argv);
        if (ret) {
            printf("factory test failed with code: %d.  Aborting boot\n",
                    ret);
            return ret;
        }
    }
#endif  // CONFIG_BEEP_GPIO_BTN_FACT_TEST_BIT

    cmd_argv[0] = "bootm";
    cmd_argv[2] = NULL;

    BEEP_BOOTB_PREBOOT();

    if (boot_primary) {
        cmd_argv[1] = getenv("beep_primary");
        if (!cmd_argv[1]) {
            puts("Using default beep_primary\n");
            setenv("beep_primary", ADDR_TO_STR(CONFIG_DEFAULT_PRIMARY_ADDR));
            cmd_argv[1] = getenv("beep_primary");
        }

        // Only increment bootcount and enable watchdog for primary image.
        inc_bootcount();

#ifdef CONFIG_BEEP_BOOTB_WATCHDOG_ENABLE
        // Only enable watchdog for primary image when watchdog = 1.
        watchdog_env = getenv("watchdog");
        if (watchdog_env && watchdog_env[0] == '1') {
            BOOTB_WATCHDOG_ENABLE();
        }
#endif  // CONFIG_BEEP_BOOTB_WATCHDOG_ENABLE

        // Only returns on error.
        do_bootm(cmdtp, 0, 2, cmd_argv);

#ifdef CONFIG_BEEP_BOOTB_WATCHDOG_ENABLE
        if (watchdog_env && watchdog_env[0] == '1') {
            // Disable watchdog on primary image error.
            BOOTB_WATCHDOG_DISABLE();
        }
#endif  // CONFIG_BEEP_BOOTB_WATCHDOG_ENABLE

        puts("Booting primary partition failed using recovery partition\n");
    } else {
        if (bc < MAX_BOOTCOUNT) {
            puts("Reset button held booting recovery partition\n");
        } else {
            puts("Bootcount exceeded booting recovery partition\n");
        }
    }

    cmd_argv[1] = getenv("beep_recovery");
    if (!cmd_argv[1]) {
        puts("Using default beep_recovery\n");
        setenv("beep_recovery", ADDR_TO_STR(CONFIG_DEFAULT_RECOVERY_ADDR));
        cmd_argv[1] = getenv("beep_recovery");
    }
    do_bootm(cmdtp, 0, 2, cmd_argv);

    puts("Failed to boot either partitions\n");
    return 1;
}

U_BOOT_CMD(bootb, 1, 0, do_bootb,
        "bootb   - Boot Beep images\n",
        "Boot Beep images\n"
);
