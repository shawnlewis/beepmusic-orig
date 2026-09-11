#include <assert.h>
#include <stdio.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "gpiopoll.h"
#include "registers.h"

#define BEEP_SCHEDULE_US 20000

typedef struct {
    int led;
    int on_frac;  // stored as integer, / BEEP_FRACTION_BASE to get fraction
} BeepLEDState;

BeepLEDState led_states[NUM_OUTPUTS + NUM_INPUT_OUTPUTS];
BeepLEDState* old_states;
int num_leds = 0;

typedef struct {
    int led;
    bool on;
    int wait_us;
} BeepLEDEvent;

pthread_mutex_t gpiopoll_mutex;
pthread_t thread;

void* gpio_page;
uint32_t input_mask = 0;
uint32_t output_mask = 0;

// Schedule is for each BEEP_SCHEDULE_MS ms window, we have two of them
// so we can quickly swap schedules without interrupting the gpio thread.
BeepLEDEvent schedule[2][1000];
int active_schedule = 0;
bool sched_updated = false;
bool use_gpio = true;
bool kill_thread = false;
static bool started = false;

void print_states(void) {
    for (int i=0; i<num_leds; i++) {
        printf("LED: %d on_frac: %d\n",
                led_states[i].led, led_states[i].on_frac);
    }
    printf("\n");
}

void print_schedule(BeepLEDEvent* sched) {
    for (BeepLEDEvent* event = &sched[0]; event->wait_us != -1; event++) {
        printf("LED: %d wait_us: %d on: %d\n",
                event->led, event->wait_us, event->on);
    }
    printf("\n");
}

// This thread doesn't actually interact with gpio, it just prints LED
// state changes. Can be used for testing on Linux VMs.
void* beep_gpio_nogpio_thread(void* arg) {
    input_cb input_callback = arg;
    while (!kill_thread) {
        char c;
        scanf(" %c", &c);
        if (c == 'a') {
            input_callback(1, true);
        }
        //if (c == 's') {
        //    input_callback(ROT_SWITCH, true);
        //}
        //if (c == 'd') {
        //    input_callback(SWITCH_LED1, true);
        //}
        //if (c == 'f') {
        //    input_callback(SWITCH_LED2, true);
        //}
        //if (c == 'g') {
        //    input_callback(SWITCH_LED3, true);
        //}
    }

    return NULL;
}

void* beep_gpio_thread(void* arg) {
    input_cb input_callback = arg;

    int prev_inputs = -1;

    while (!kill_thread) {
        BeepLEDEvent* sched;

        pthread_mutex_lock(&gpiopoll_mutex);
        if (sched_updated) {
            active_schedule = !active_schedule;
            sched_updated = false;
        }
        sched = schedule[active_schedule];
        pthread_mutex_unlock(&gpiopoll_mutex);

        int turn_off = 0, turn_on = 0;
        for (BeepLEDEvent* event = &sched[0]; event->wait_us != -1; event++) {
            if (event->wait_us) {
                // Commit current timestamp events
                clear_bits(gpio_page, GPIO_OUT, turn_off);
                set_bits(gpio_page, GPIO_OUT, turn_on);

                usleep(event->wait_us);

                turn_on = 0;
                turn_off = 0;
            }
            if (event->led != -1) {
                if (event->on) {
                    turn_on |= event->led;
                } else {
                    turn_off |= event->led;
                }
            }
        }

        ///// Read inputs

        // First save the current outputs
        uint32_t old_outputs = read_word(gpio_page, GPIO_OUT);
        clear_bits(gpio_page, GPIO_OUT, output_mask);

        // Switch inputs to inputs, read them, then switch back to outputs
        clear_bits(gpio_page, GPIO_OE, input_mask);
        int inputs = read_word(gpio_page, GPIO_IN) & input_mask;
        set_bits(gpio_page, GPIO_OE, output_mask);

        // Restore old outputs
        set_bits(gpio_page, GPIO_OUT, old_outputs);

        int new_inputs = 0;
        if (prev_inputs == -1) {
            // Send initial states
            new_inputs = input_mask;
        } else if (inputs != prev_inputs) {
            new_inputs = (inputs ^ prev_inputs);
        }
        if (new_inputs) {
            int mask = 1;
            for (int i=0; i<32; i++) {
                if (new_inputs & mask) {
                    input_callback(i, !!(inputs & mask));
                }
                mask <<= 1;
            }
        }
        prev_inputs = inputs;
    }

    return NULL;
}

BeepLEDEvent make_event(int led, int wait_us, bool on) {
    BeepLEDEvent event;
    event.led = led;
    event.wait_us = wait_us;
    event.on = on;
    return event;
}

void beep_gpio_init(bool _use_gpio) {
    use_gpio = _use_gpio;

    // Initial schedule
    schedule[0][0] = make_event(-1, BEEP_SCHEDULE_US, false);
    schedule[0][1] = make_event(-1, -1, false);
    schedule[1][0] = make_event(-1, BEEP_SCHEDULE_US, false);
    schedule[1][1] = make_event(-1, -1, false);

    if (use_gpio) {
        gpio_page = map_page(GPIO_BASE_ADDRESS);
    }
}


bool beep_gpio_enable_input(int gpio) {
    int ret = set_output_enable(gpio_page, gpio, false);
    if (ret) {
        input_mask |= (1 << gpio);
    }
    return ret;
}

bool beep_gpio_enable_output(int gpio) {
    int ret = set_output_enable(gpio_page, gpio, true);
    if (ret) {
        led_states[num_leds].led = (1 << gpio);
        num_leds++;
        output_mask |= (1 << gpio);
    }
    return ret;
}

void beep_gpio_start(input_cb input_callback) {
    // start thread
    if (!started) {
        started = true;
        if (!use_gpio) {
            printf("NON-GPIO\n");
            old_states = malloc(sizeof(led_states));
            memcpy(old_states, led_states, sizeof(led_states));
            pthread_create(&thread, NULL, beep_gpio_nogpio_thread, input_callback);
        } else {
            printf("GPIO\n");
            pthread_create(&thread, NULL, beep_gpio_thread, input_callback);
        }
    }
}

void beep_gpio_end(void) {
    if (started) {
        kill_thread = true;
        pthread_cancel(thread);
        started = false;
    }
}

bool beep_gpio_set_led_on_frac(int gpio, int on_frac) {
    int led = (1 << gpio);
    for (int i=0; i<num_leds; i++) {
        if (led == led_states[i].led) {
            led_states[i].on_frac = on_frac;
            pthread_mutex_unlock(&gpiopoll_mutex);
            return true;
        }
    }
    return false;
}

int compare_led_frac(const void* arg1, const void* arg2) {
    int on_frac1 = ((BeepLEDState*) arg1)->on_frac;
    int on_frac2 = ((BeepLEDState*) arg2)->on_frac;
    if (on_frac1 < on_frac2) {
        return -1;
    } else if (on_frac1 == on_frac2) {
        return 0;
    } else {
        return 1;
    }
}

void beep_gpio_commit_leds() {
    if (!use_gpio) {
        bool diff = false;
        for (int i=0; i<num_leds; i++) {
            for (int j=0; j<num_leds; j++) {
                if (old_states[i].led == led_states[j].led) {
                    if (old_states[i].on_frac != led_states[j].on_frac) {
                        diff = true;
                    }
                    old_states[i].on_frac = led_states[j].on_frac;
                }
            }
        }
        if (diff) {
            print_states();
        }
    }
    qsort(&led_states, num_leds, sizeof(BeepLEDState), compare_led_frac);

    pthread_mutex_lock(&gpiopoll_mutex);

    BeepLEDEvent* sched = schedule[!active_schedule];
    sched_updated = true;

    int cur_event = 0;

    // Set initial states.
    for (int i=0; i<num_leds; i++) {
        BeepLEDState* state = &led_states[i];
        if (state->on_frac == 0) {
            sched[cur_event++] = make_event(state->led, 0, false);
        } else {
            sched[cur_event++] = make_event(state->led, 0, true);
        }
    }

    // Turn off on LEDs at the appropriate times.
    int time_us = 0;
    for (int i=0; i<num_leds; i++) {
        BeepLEDState* state = &led_states[i];
        if (state->on_frac == 0) {
            continue;
        }
        int event_time_us =
            (BEEP_SCHEDULE_US * state->on_frac / BEEP_FRACTION_BASE);
        int delta = event_time_us - time_us;
        sched[cur_event++] = make_event(state->led, delta, false);
        time_us += delta;
    }

    // Remainder of schedule is a NOOP for the rest of the time.
    if (time_us != BEEP_SCHEDULE_US) {
        sched[cur_event++] = make_event(
                -1, (BEEP_SCHEDULE_US - time_us), false);
    }

    // Schedule end.
    sched[cur_event++] = make_event(-1, -1, false);

    //print_schedule(sched);

    pthread_mutex_unlock(&gpiopoll_mutex);
}


//int main(int argc, char **argv) {
//    beep_gpio_init(input_callback);
//    //while (1) {
//    //    for (int l=0; l<num_leds; l++) {
//    //        for (int i=100000; i>0; i/=1.03) {
//    //            beep_gpio_set_led_on_frac(LEDS[l], i);
//    //            beep_gpio_commit_leds();
//    //            //print_states();
//    //            usleep(5000);
//    //        }
//    //        beep_gpio_set_led_on_frac(LEDS[l], 0);
//    //        beep_gpio_commit_leds();
//    //    }
//    //}
//
//    for (int l=0; l<num_leds; l++) {
//        beep_gpio_set_led_on_frac(LEDS[l], 5000);
//        beep_gpio_commit_leds();
//    }
//    while (1) {
//        sleep(100);
//    }
//
//    return 0;
//}
