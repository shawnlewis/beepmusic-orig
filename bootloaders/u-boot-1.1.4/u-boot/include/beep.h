#ifndef BEEP_H
#define BEEP_H

#ifndef __ASSEMBLER__

// Offset Beep definitions to ensure they are unique, though limit
// calling with these offsets into non-Beep boards.
#define BEEP_GPIO_ID_OFFSET                         (0x1000)

#define BEEP_GPIO_ACTIVE_LOW                        (0)
#define BEEP_GPIO_ACTIVE_HIGH                       (1)

#define BEEP_GPIO_BTN_SETUP_ID                      (BEEP_GPIO_ID_OFFSET | 0x0)
#define BEEP_GPIO_BTN_FACT_TEST_ID                  (BEEP_GPIO_ID_OFFSET | 0x1)

typedef struct {
    int id;
    int bit;
    int polarity;
} GPIOInfo;

// In $(BOARD).c
int button_read(int button_id);


#ifdef CONFIG_BEEP_FACT_TEST_ENTER_FUNC
// In $(TARGET).c
void beep_fact_test_enter(void);
#define BEEP_FACT_TEST_ENTER() beep_fact_test_enter()
#else  // CONFIG_BEEP_FACT_TEST_ENTER_FUNC
#define BEEP_FACT_TEST_ENTER()
#endif  // CONFIG_BEEP_FACT_TEST_ENTER_FUNC

#ifdef CONFIG_BEEP_FACT_TEST_EXIT_FUNC
// In $(TARGET).c.  Pass when status == 0.
void beep_fact_test_exit(int status);
#define BEEP_FACT_TEST_EXIT(__STATUS__) beep_fact_test_exit(__STATUS__)
#else  // CONFIG_BEEP_FACT_TEST_EXIT_FUNC
#define BEEP_FACT_TEST_EXIT(__STATUS__)
#endif  // CONFIG_BEEP_FACT_TEST_EXIT_FUNC

#ifdef CONFIG_BEEP_TARGET_INIT_FUNC
// In $(TARGET).c
void beep_target_init(void);
#define BEEP_TARGET_INIT() beep_target_init()
#else  // CONFIG_BEEP_TARGET_INIT_FUNC
#define BEEP_TARGET_INIT()
#endif  // CONFIG_BEEP_TARGET_INIT_FUNC

#ifdef CONFIG_BEEP_BOOTB_PREBOOT_FUNC
// In $(TARGET).c
void beep_bootb_preboot(void);
#define BEEP_BOOTB_PREBOOT() beep_bootb_preboot()
#else  // CONFIG_BEEP_BOOTB_PREBOOT_FUNC
#define BEEP_BOOTB_PREBOOT()
#endif  // CONFIG_BEEP_BOOTB_PREBOOT_FUNC


#ifdef CONFIG_BEEP_BOOTB_WATCHDOG_ENABLE
// In $(BOARD).c
void board_wdt_enable(void);
void board_wdt_disable(void);
#define BOOTB_WATCHDOG_ENABLE() board_wdt_enable()
#define BOOTB_WATCHDOG_DISABLE() board_wdt_disable()
#else  // CONFIG_BEEP_BOOTB_WATCHDOG_ENABLE
#define BOOTB_WATCHDOG_ENABLE()
#define BOOTB_WATCHDOG_DISABLE()
#endif  // CONFIG_BEEP_BOOTB_WATCHDOG_ENABLE

#endif  // __ASSEMBLER__

#endif  // BEEP_H
