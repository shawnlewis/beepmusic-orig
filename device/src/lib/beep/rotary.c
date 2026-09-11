#include <stdlib.h>

#include "rotary.h"

BeepRotaryEncoder* beep_rotary_encoder_init(void) {
    BeepRotaryEncoder* enc = malloc(sizeof(BeepRotaryEncoder));
    enc->a = BEEP_ROTARY_HIGH;
    enc->b = BEEP_ROTARY_HIGH;
    enc->pos = 0;
    enc->turning = BEEP_ROTARY_NONE;
    enc->armed = false;
    return enc;
}

BeepRotaryEncoderResult beep_rotary_encoder_change(
        BeepRotaryEncoder* enc,
        BeepRotaryEncoderEvent a,
        BeepRotaryEncoderEvent b) {
    if (a != BEEP_ROTARY_SAME) {
        enc->a = a;
    }
    if (b != BEEP_ROTARY_SAME) {
        enc->b = b;
    }
    if (enc->a == BEEP_ROTARY_HIGH && enc->b == BEEP_ROTARY_HIGH) {
        if (enc->armed) {
            enc->armed = false;
            if (enc->turning == BEEP_ROTARY_CLOCKWISE) {
                enc->pos++;
            } else {
                enc->pos--;
            }
            return enc->turning;
        }
    } else if (enc->a == BEEP_ROTARY_HIGH && enc->b == BEEP_ROTARY_LOW) {
        enc->turning = BEEP_ROTARY_CLOCKWISE;
    } else if (enc->a == BEEP_ROTARY_LOW && enc->b == BEEP_ROTARY_HIGH) {
        enc->turning = BEEP_ROTARY_COUNTERCLOCKWISE;
    } else {
        enc->armed = true;
    }
    return BEEP_ROTARY_NONE;
}
