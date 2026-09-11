#ifndef BEEP_GPIOPOLL_H
#define BEEP_GPIOPOLL_H

#define BEEP_FRACTION_BASE 1000

#include <stdbool.h>

typedef void (*input_cb)(int input, bool on);

void beep_gpio_init(bool _use_gpio);
bool beep_gpio_enable_input(int gpio);
bool beep_gpio_enable_output(int gpio);
void beep_gpio_start(input_cb input_callback);
void beep_gpio_end(void);
bool beep_gpio_set_led_on_frac(int led, int on_frac);
void beep_gpio_commit_leds(void);

#endif  // BEEP_GPIOPOLL_H
