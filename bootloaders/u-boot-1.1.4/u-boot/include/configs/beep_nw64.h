#ifndef CONFIG_BEEP_NW64_H
#define CONFIG_BEEP_NW64_H

// Need to be defined before beep.h.
#define CONFIG_BEEP_TARGET_INIT_FUNC                1
#define CONFIG_BEEP_BOOTB_PREBOOT_FUNC              1

#include "configs/ap121-beep.h"
#include "beep.h"

#define CONFIG_BOARD_BEEP_NW64
#define CONFIG_BEEP_BOARD_STRING                    "BEEP_NW64"
#define CONFIG_DEFAULT_RECOVERY_ADDR                0x9f050000
#define CONFIG_DEFAULT_PRIMARY_ADDR                 0x9f550000
#define CONFIG_NULL_DEVICE                          1

#define CONFIG_BEEP_GPIO_BTN_SETUP_BIT              6
#define CONFIG_BEEP_GPIO_BTN_SETUP_POLARITY         BEEP_GPIO_ACTIVE_LOW

#define CONFIG_BEEP_FACT_TEST_MEM_START             "0x80200000"
#define CONFIG_BEEP_FACT_TEST_MEM_END               "0x83dfffff"

#define CFG_CONSOLE_IS_IN_ENV                       1
#define CFG_ALT_MEMTEST                             1

#endif  // CONFIG_BEEP_NW64_H
