#ifndef BEEP_ROTARY_H
#define BEEP_ROTARY_H

#include <stdbool.h>

typedef enum {
    BEEP_ROTARY_SAME,
    BEEP_ROTARY_HIGH,
    BEEP_ROTARY_LOW
} BeepRotaryEncoderEvent;

typedef enum {
    BEEP_ROTARY_NONE,
    BEEP_ROTARY_CLOCKWISE,
    BEEP_ROTARY_COUNTERCLOCKWISE
} BeepRotaryEncoderResult;

typedef struct {
    BeepRotaryEncoderEvent a;
    BeepRotaryEncoderEvent b;
    int pos;
    BeepRotaryEncoderResult turning;
    bool armed;
} BeepRotaryEncoder;

BeepRotaryEncoder* beep_rotary_encoder_init(void);

BeepRotaryEncoderResult beep_rotary_encoder_change(
        BeepRotaryEncoder* enc,
        BeepRotaryEncoderEvent a,
        BeepRotaryEncoderEvent b);

#endif  // BEEP_ROTARY_H
