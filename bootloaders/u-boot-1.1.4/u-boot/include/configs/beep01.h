#ifndef CONFIG_BEEP01_H
#define CONFIG_BEEP01_H

// Need to be defined before beep.h.
#define CONFIG_BEEP_FACT_TEST_EXIT_FUNC             1

#include "configs/ap121-beep.h"
#include "beep.h"

#define CONFIG_BOARD_BEEP01
#define CONFIG_BEEP_BOARD_STRING                    "BEEP01"
#define CONFIG_DEFAULT_RECOVERY_ADDR                0x9f050000
#define CONFIG_DEFAULT_PRIMARY_ADDR                 0x9f550000
#define CONFIG_NULL_DEVICE                          1

// WileE SW3
#define CONFIG_BEEP_GPIO_BTN_SETUP_BIT              26
#define CONFIG_BEEP_GPIO_BTN_SETUP_POLARITY         BEEP_GPIO_ACTIVE_LOW
// WileE SW1
#define CONFIG_BEEP_GPIO_BTN_FACT_TEST_BIT          11
#define CONFIG_BEEP_GPIO_BTN_FACT_TEST_POLARITY     BEEP_GPIO_ACTIVE_LOW

#define CONFIG_BEEP_FACT_TEST_MEM_START             "0x80200000"
#define CONFIG_BEEP_FACT_TEST_MEM_END               "0x83dfffff"

#define CFG_CONSOLE_IS_IN_ENV                       1
#define CFG_ALT_MEMTEST                             1

#endif  // CONFIG_BEEP01_H
