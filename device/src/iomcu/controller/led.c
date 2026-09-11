#include "stm8l15x.h"

#include "controller.h"


#define LED_OFFSET(ROW, COL)        (((ROW) * LED_COL_SIZE) + COL)
#define LED_ARR_IDX(ARR, IDX, I) \
    (ARR)[(IDX)[(I)] + (((I) / LED_COL_SIZE) * LED_COL_SIZE)]

// This doesn't work with IARs preprocessor.
//#ifndef LED_ROW_COL_SWAP
//#if LED_ROW_0 != GPIO_Pin_0 || \
//    LED_ROW_1 != GPIO_Pin_1 || \
//    LED_ROW_2 != GPIO_Pin_2 || \
//    LED_ROW_3 != GPIO_Pin_3 || \
//    LED_COL_0 != GPIO_Pin_0 || \
//    LED_COL_1 != GPIO_Pin_1 || \
//    LED_COL_2 != GPIO_Pin_2 || \
//    LED_COL_3 != GPIO_Pin_3 || \
//    LED_COL_4 != GPIO_Pin_4 || \
//    LED_COL_5 != GPIO_Pin_5
//#else  // LED_ROW_COL_SWAP
//#if LED_ROW_0 != GPIO_Pin_0 || \
//    LED_ROW_1 != GPIO_Pin_1 || \
//    LED_ROW_2 != GPIO_Pin_2 || \
//    LED_ROW_3 != GPIO_Pin_3 || \
//    LED_ROW_4 != GPIO_Pin_4 || \
//    LED_ROW_5 != GPIO_Pin_5 || \
//    LED_COL_0 != GPIO_Pin_0 || \
//    LED_COL_1 != GPIO_Pin_1 || \
//    LED_COL_2 != GPIO_Pin_2 || \
//    LED_COL_3 != GPIO_Pin_3
//#endif  // LED_ROW_COL_SWAP
//#error "LED array bitmask assumptions may be incorrect"
//#endif

#define LED_ARR_DELTA_MIN   (1)
#define LED_MAX_ROW_ARR     (255)

#ifdef LED_10_BIT_PWM
// ARR = ((f/prescaler)(.001024/1024)d) - 1
// ARR = (d / 4) - 1
// f = timer frequency (16e6 Hz)
// prescaler = 64
// d = duty cycle (0 - 1024)
// Single LED at d(1024) has duty cycle == 25%; f == 976.5625 Hz
#define CALC_ARR(__DUTY__) \
    ((uint8_t)((((__DUTY__) < (4 * (LED_ARR_DELTA_MIN + 1))) ? 0 : \
    ((__DUTY__) > (1024 - (4 * (LED_ARR_DELTA_MIN + 1)))) ? LED_MAX_ROW_ARR : \
    (__DUTY__ / 4) - 1)))
#else  // LED_10_BIT_PWM
#define CALC_ARR(__DUTY__) (__DUTY__)
#endif  // LED_10_BIT_PWM

#define SWAP_IDX(D, I, A, B) \
    if (D[I[A]] > D[I[B]]) { \
        I[A] ^= I[B]; \
        I[B] ^= I[A]; \
        I[A] ^= I[B]; \
    }

LedArrayData led_data[LED_TABLE_COUNT];
uint8_t led_state;
LedArrayCalc *led_calc_table;
LedArrayPWM *led_pwm_table;
LedArrayOp *led_ops_table;

#ifndef LED_ROW_COL_SWAP
static const uint8_t led_lut[LED_TOT_SIZE] = {
    9, 13, 17, 21, 1, 5,
    8, 12, 16, 20, 0, 4,
    7, 11, 15, 19, 23, 3,
    6, 10, 14, 18, 22, 2
};
#else // LED_ROW_COL_SWAP
static const uint8_t led_lut[LED_TOT_SIZE] = {
    9, 8, 7, 6, 13, 12,
    11, 10, 17, 16, 15, 14,
    21, 20, 19, 18, 1, 0,
    23, 22, 5, 4, 3, 2,
};
#endif   // LED_ROW_COL_SWAP

void led_data_init(void) {
    led_state = 0;
    led_calc_table = LED_TABLE_CALC;
    led_pwm_table = LED_TABLE_PWM_A;
    led_ops_table = LED_TABLE_OPS_B;
    led_current_op = LED_TABLE_OPS_B;

#ifndef LED_SPLASH_BOOT
    uint8_t i;

    for (i = 0; i < LED_ROW_SIZE; i++) {
        led_ops_table[i].arr = LED_MAX_ROW_ARR;
        led_ops_table[i].op = LED_OP_ROW_INC | LED_COL_ALL;
    }

#ifdef LED_D11_BOOT
#ifndef LED_ROW_COL_SWAP
    led_ops_table[1].op = 0x7d;
#else // LED_ROW_COL_SWAP
    led_ops_table[1].op = 0x4d;
#endif   // LED_ROW_COL_SWAP
#endif  // LED_D17_BOOT

    led_ops_table[i - 1].op |= LED_OP_LOOP;

#else  // LED_SPLASH_BOOT

#ifndef LED_ROW_COL_SWAP
    led_ops_table[0].arr = LED_MAX_ROW_ARR;
    led_ops_table[0].op = 0x74;
    led_ops_table[1].arr = LED_MAX_ROW_ARR;
    led_ops_table[1].op = 0x78;
    led_ops_table[2].arr = LED_MAX_ROW_ARR;
    led_ops_table[2].op = 0x78;
    led_ops_table[3].arr = LED_MAX_ROW_ARR;
    led_ops_table[3].op = 0xd9;
#else // LED_ROW_COL_SWAP
    led_ops_table[0].arr = LED_MAX_ROW_ARR;
    led_ops_table[0].op = 0x48;
    led_ops_table[1].arr = LED_MAX_ROW_ARR;
    led_ops_table[1].op = 0x40;
    led_ops_table[2].arr = LED_MAX_ROW_ARR;
    led_ops_table[2].op = 0x41;
    led_ops_table[3].arr = LED_MAX_ROW_ARR;
    led_ops_table[3].op = 0x4e;
    led_ops_table[4].arr = LED_MAX_ROW_ARR;
    led_ops_table[4].op = 0x4f;
    led_ops_table[5].arr = LED_MAX_ROW_ARR;
    led_ops_table[5].op = 0xc7;
#endif   // LED_ROW_COL_SWAP

#endif  // LED_SPLASH_BOOT
}

static inline void swap_row(uint8_t *arr, uint8_t *idx) {
#if LED_COL_SIZE == 6
    //Bose-Nelson Algorithm (n == 6).
    SWAP_IDX(arr, idx, 1, 2);
    SWAP_IDX(arr, idx, 0, 2);
    SWAP_IDX(arr, idx, 0, 1);
    SWAP_IDX(arr, idx, 4, 5);
    SWAP_IDX(arr, idx, 3, 5);
    SWAP_IDX(arr, idx, 3, 4);
    SWAP_IDX(arr, idx, 0, 3);
    SWAP_IDX(arr, idx, 1, 4);
    SWAP_IDX(arr, idx, 2, 5);
    SWAP_IDX(arr, idx, 2, 4);
    SWAP_IDX(arr, idx, 1, 3);
    SWAP_IDX(arr, idx, 2, 3);
#elif LED_COL_SIZE == 4
    //Bose-Nelson Algorithm (n == 4).
    SWAP_IDX(arr, idx, 0, 1);
    SWAP_IDX(arr, idx, 2, 3);
    SWAP_IDX(arr, idx, 0, 2);
    SWAP_IDX(arr, idx, 1, 3);
    SWAP_IDX(arr, idx, 1, 2);
#else
#error "Unknown sorting network."
#endif
}

void led_update_arr(void) {
    uint8_t i;
    uint8_t j = 0;
    uint8_t t = 0;
    LedArrayOp *p = (led_ops_table == LED_TABLE_OPS_A) ?
            LED_TABLE_OPS_B : LED_TABLE_OPS_A;

    // Calculate ARR values from PWM.
    for (i = 0; i < LED_TOT_SIZE; i++) {
        led_calc_table->arr[i] = CALC_ARR(led_pwm_table[led_lut[i]]);
        //led_calc_table->arr[i] = CALC_ARR(led_pwm_table[i]);
        led_calc_table->idx[i] = t;
        t = (t == (LED_COL_SIZE - 1)) ? 0 : t + 1;
    }

    // Sort each row in ascending order of ARR using indexes to track
    // LEDs->ARR.  Calculate remaining row ARR for possible delay with
    // all LEDs off.
    for (i = 0; i < LED_ROW_SIZE; i++) {
        swap_row(led_calc_table->arr + LED_OFFSET(i, 0), led_calc_table->idx
                + LED_OFFSET(i, 0));
        led_calc_table->rarr[i] = LED_MAX_ROW_ARR
                - LED_ARR_IDX(led_calc_table->arr, led_calc_table->idx,
                LED_OFFSET(i, LED_COL_SIZE - 1));
    }

    // Combine ARR values if the delta between them is too small for
    // the stm8l to interrupt fast enough.
    for (i = 0; i < LED_ROW_SIZE; i++) {
        t = 0;
        for (j = 0; j < LED_COL_SIZE; j++) {
            if ((LED_ARR_IDX(led_calc_table->arr, led_calc_table->idx,
                    LED_OFFSET(i, j)) - t) >= LED_ARR_DELTA_MIN) {
                t = LED_ARR_IDX(led_calc_table->arr, led_calc_table->idx,
                        LED_OFFSET(i, j));
            } else {
                LED_ARR_IDX(led_calc_table->arr, led_calc_table->idx,
                        LED_OFFSET(i, j)) = t;
            }
        }
    }

    // Convert absolute ARR values into deltas from the previous ARR.
    for (i = 0; i < LED_ROW_SIZE; i++) {
        t = 0;
        for (j = 0; j < LED_COL_SIZE; j++) {
            LED_ARR_IDX(led_calc_table->arr, led_calc_table->idx,
                    LED_OFFSET(i, j)) =
                    LED_ARR_IDX(led_calc_table->arr, led_calc_table->idx,
                    LED_OFFSET(i, j)) - t;
            t = LED_ARR_IDX(led_calc_table->arr, led_calc_table->idx,
                    LED_OFFSET(i, j)) + t;
        }
    }

    // Generate a schedule for TIM4 to follow.
    // Schedule:
    // 1) Turn off LEDs marked with 1 in (op & LED_OP_COL_MASK)
    // 2) Set ARR register with arr
    // 3) Wait
    // 4) Process ROW_INC and LOOP.
    // 5) Move to next op.
    // Each op is actualy processed in two interrupts, while a single
    // interrupt processes parts of two ops.
    for (i = 0; i < LED_ROW_SIZE; i++) {
        t = 0;
        for (j = 0; j < LED_COL_SIZE; j++) {
            // Delay to process, reset marked LEDs.
            if (LED_ARR_IDX(led_calc_table->arr, led_calc_table->idx,
                    LED_OFFSET(i, j)) != 0) {
                p->arr = LED_ARR_IDX(led_calc_table->arr, led_calc_table->idx,
                        LED_OFFSET(i, j));
                p->op = t;
                t = 0;
                p++;
            }
            // Mark LEDs while moving through each row.
            t |= 1 << (led_calc_table->idx[LED_OFFSET(i, j)]);
        }
        // After each row check if an additional delay is needed.  If
        // not set the ROW_INC marker in the previous op.
        if (led_calc_table->rarr[i]) {
            p->arr = led_calc_table->rarr[i];
            p->op = LED_OP_ROW_INC | t;
            p++;
        } else {
            (p - 1)->op |= LED_OP_ROW_INC;
        }
    }
    // Mark the last op with LOOP.
    (p - 1)->op |= LED_OP_LOOP;

    // Trigger to TIM4 new schedule is ready.  Do not clear PWM_READY
    // so this function does not get called again, and the new schedule
    // is actually pointed to by led_pwm_table.
    led_state |= LED_STATE_OPS_READY;
}
