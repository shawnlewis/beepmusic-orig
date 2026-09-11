#include <stdio.h>
#include <unistd.h>

#include "beep/gpiopoll.h"
#include "beep/registers.h"

#define LOG(...) fprintf (stderr, __VA_ARGS__)


void input_callback(int input, bool on) {
    LOG("Input change detected. gpio: %d, val: %d\n", input, on);
}

void read_mode(void) {
    beep_gpio_init(true);
    for (int i = 0; i < NUM_INPUT_OUTPUTS; i++) {
        beep_gpio_enable_input(INPUT_OUTPUTS[i]);
    }
    beep_gpio_start(input_callback);

    while (1) {
        sleep(1);
    }
}

void write_mode(void) {
    beep_gpio_init(true);
    for (int i = 0; i < NUM_INPUT_OUTPUTS; i++) {
        beep_gpio_enable_output(INPUT_OUTPUTS[i]);
    }
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        beep_gpio_enable_output(OUTPUTS[i]);
    }

    beep_gpio_start(input_callback);

    //for (int i = 0; i < NUM_INPUT_OUTPUTS; i++) {
    //    beep_gpio_set_led_on_frac(INPUT_OUTPUTS[i], 5);
    //}
    //for (int i = 0; i < NUM_OUTPUTS; i++) {
    //    beep_gpio_set_led_on_frac(OUTPUTS[i], 5);
    //}
    //beep_gpio_commit_leds();
    //while (1) {
    //    sleep(1);
    //}

    int iteration = 0;
    while (1) {
        int step = iteration % 96;
        int unscaled_on_frac = step < 48 ? step * 20 : (96 - step) * 20;

        int on_frac = 1000;
        for (int i = 0; i < 3; i++) {
            on_frac = on_frac * unscaled_on_frac / 1000;
        }

        //LOG("on_frac %d\n", on_frac);
        for (int i = 0; i < NUM_INPUT_OUTPUTS; i++) {
            beep_gpio_set_led_on_frac(INPUT_OUTPUTS[i], on_frac);
        }
        for (int i = 0; i < NUM_OUTPUTS; i++) {
            beep_gpio_set_led_on_frac(OUTPUTS[i], on_frac);
        }
        beep_gpio_commit_leds();
        usleep(41000);
        iteration++;
    }
}

int main(int argc, char** argv) {
    if (argc > 1) {
        LOG("Argument detected, running in read mode\n");
        read_mode();
    } else {
        LOG("No arguments detected, running in write mode. Pass any arg to\n"
            "run in write mode\n\n");
        write_mode();
    }
}
