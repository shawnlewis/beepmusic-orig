#include "stm8l15x.h"

#include "controller.h"


uint8_t knob_psense_dma_buf[KNOB_PSENSE_DMA_BUF_SIZE];

static uint8_t knob_switch_state = 0xff;  // Set to normal switch state.

#define KNOB_SWITCH_DOWN                            (0)
#define KNOB_SWITCH_UP                              (1)

static uint8_t knob_switch_count[2] = {0, 0};
static int8_t knob_encoder_count = 0;

uint8_t adc_proc_state;

void knob_switch_read(void) {
    knob_switch_state <<= 1;
    knob_switch_state |= GPIO_STATE_HIGH(KNOB_SW_PORT, KNOB_SW_PIN);
    if ((knob_switch_state & 0xf) == 0x3) {
        knob_switch_count[KNOB_SWITCH_UP]++;
    } else if ((knob_switch_state & 0xf) == 0xc) {
        knob_switch_count[KNOB_SWITCH_DOWN]++;
    }
}

uint8_t knob_switch_read_down_count(void) {
    uint8_t ret = knob_switch_count[KNOB_SWITCH_DOWN];
    knob_switch_count[KNOB_SWITCH_DOWN] -= ret;
    return ret;
}

uint8_t knob_switch_read_up_count(void) {
    uint8_t ret = knob_switch_count[KNOB_SWITCH_UP];
    knob_switch_count[KNOB_SWITCH_UP] -= ret;
    return ret;
}

#define UP_VAL_THRESHOLD 100

#define DOWN_SEQ_THRESHOLD 40
#define UP_SEQ_THRESHOLD 10

// whether the previous sample was up, per channel
static uint8_t prev_up[2] = {0, 0};

// count of up or down values in a row, per channel
static uint32_t run_count[2] = {0, 0};

// is_up: whether we are currently returning an up signal or down signal, per
// channel
static uint8_t sending_up[2] = {0, 0};

static uint8_t channel_vals[2] = {0, 0};

// bitfield of channel_values
static uint8_t prev_encoder_state = 0;

// have we passed through state 0x2?
static uint8_t encoder_armed = 0;

// direction of the current turn
static int8_t direction = 0;

static inline int8_t find_turns(uint8_t channel, uint8_t is_up) {
    channel_vals[channel] = is_up;
    uint8_t encoder_state = channel_vals[1] << 1 | channel_vals[0];
    if (encoder_state == prev_encoder_state) {
        return 0;
    }
    prev_encoder_state = encoder_state;

    switch (encoder_state) {
    case 0x0:
        if (encoder_armed) {
            encoder_armed = 0;
            return direction;
        }
        break;
    case 0x1:
        if (!encoder_armed) {
            direction = -1;
        } else if (direction != 1) {
            encoder_armed = 0;
        }
        break;
    case 0x2:
        if (!encoder_armed) {
            direction = 1;
        } else if (direction != -1) {
            encoder_armed = 0;
        }
        break;
    case 0x3:
        encoder_armed = 1;
        break;
    }

    return 0;
}

// should be called with alternating channels
static int8_t add_point(uint8_t channel, uint8_t value) {
    uint8_t up = value > UP_VAL_THRESHOLD;
    if (prev_up[channel] != up) {
        run_count[channel] = 0;
    } else {
        run_count[channel]++;
    }
    prev_up[channel] = up;
    if (up && run_count[channel] > UP_SEQ_THRESHOLD) {
        sending_up[channel] = 1;
    } else if (!up && run_count[channel] > DOWN_SEQ_THRESHOLD) {
        sending_up[channel] = 0;
    }
    return find_turns(channel, sending_up[channel]);
}

void knob_encoder_proc_data(uint8_t half) {
    uint8_t i = half ? 0 : 120;
    uint8_t last = half ? 120 : 240;

    for (; i < last; i++) {
        int8_t val = add_point(i & 1, knob_psense_dma_buf[i]);
        if (val)
            knob_encoder_count += val;
    }
}

int8_t knob_encoder_read_count(void) {
    int8_t ret = knob_encoder_count;
    if (ret)
        knob_encoder_count -= ret;
    return ret;
}

#ifdef OPTICAL_ENCODER_DEBUG
uint8_t table_0_ready;
uint8_t table_1_ready;
uint8_t compressed_table_0_ready;
uint8_t compressed_table_1_ready;
uint8_t knob_psense_dma_comp_buf[KNOB_PSENSE_DMA_COMP_BUF_SIZE];

void knob_encoder_compress_data(uint8_t half) {
    uint8_t i = half ? 0 : 30;
    uint8_t last = half ? 30 : 60;

    for (; i < last; i++) {
        uint8_t buf_off = i * 4;
        uint8_t comp_buf_off = i * 3;

        // Compress every 4 bytes into 3 bytes, dropping 2 bits per byte.
        knob_psense_dma_comp_buf[comp_buf_off] =
                (knob_psense_dma_buf[buf_off] & 0xfc)
                | (knob_psense_dma_buf[buf_off + 1] >> 6);
        knob_psense_dma_comp_buf[comp_buf_off + 1] =
                ((knob_psense_dma_buf[buf_off + 1] << 2) & 0xf0)
                | (knob_psense_dma_buf[buf_off + 2] >> 4);
        knob_psense_dma_comp_buf[comp_buf_off + 2] =
                ((knob_psense_dma_buf[buf_off + 2] << 4) & 0xc0)
                | (knob_psense_dma_buf[buf_off + 3] >> 2);
    }

    if (half) {
        compressed_table_0_ready = 0;
        compressed_table_1_ready = 1;
    } else {
        compressed_table_1_ready = 0;
        compressed_table_0_ready = 1;
    }
}
#endif  // OPTICAL_ENCODER_DEBUG
