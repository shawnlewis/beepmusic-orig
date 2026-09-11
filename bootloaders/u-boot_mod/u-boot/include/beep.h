#ifndef BEEP_H
#define BEEP_H

// Returns:
// 0-3: Number of boots before reset by kernel.
// -1: No empty space left.
// -2: Invalid value found (this value will be cleared).
int get_bootcount(void);
void inc_bootcount(void);

#ifdef GPIO_TEST_MODE_BIT
// Returns:
// 1 - test mode active.
// 0 - test mode not active.
int test_mode_gpio_status(void);
void set_test_mode_ok_led(int enable);
#endif  // GPIO_TEST_MODE_BIT

#define MAX_BOOTCOUNT                               (3)
#define RESET_BUTTON_MS_DELAY                       (500)
#define RESET_BUTTON_MAX_COUNT                      (10)

#endif  // BEEP_H
