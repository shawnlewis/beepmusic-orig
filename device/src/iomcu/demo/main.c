#include "stm8l15x.h"

#include "demo.h"


#define I2C_SYS_INFO_DEMO_BOARD_REV                 (0x0001)


// Interrupts feed system changes into live_state.  When the Beep module
// requests the system state via i2c live_state is copied to latched_state,
// and live_state is cleared.  This also happens in an interrupt handler so
// concurrency control is not needed.
static DEMO_SYSTEM_STATE live_state;
static DEMO_SYSTEM_STATE latched_state;

static uint8_t live_state_data;

static uint8_t master_seq;
static uint8_t slave_seq;
static uint8_t send_ack;
static uint8_t waiting_for_ack;

static uint8_t beep_module_led_control;

static void demo_init(void) {
    uint8_t i;

    // Init basic stm hardware.
    stm_hw_init();

    // Get initial button and ac batt state.  Before full starting the
    // hardware and interrupts to prevent double counting data.
    for (i = 0; i < __DEMO_BTN_COUNT; i++) {
        // Only need to check if buttons are being held down
        // on on boot.  Ignore the default state.
        if (stm_get_button_state((DEMO_BTN)i) == BTN_STATE_DOWN) {
            live_state.buttons[i].down_count++;
            live_state_data = 1;
        }
    }

    // Get the power source state but do not set initial_data.
    live_state.pwr_source = stm_get_power_source_state();

    // Start stm hardware.
    stm_hw_start();

    // Now that the hardware is started if there was any initial data
    // set the data ready line.
    if (live_state_data) {
        stm_set_data_ready(DATA_READY_YES);
    }
}

void main(void) {
    demo_init();

    // While the Beep module is booting set the timer callback for every
    // half second so we can flash the LEDs from the mcu.
    stm_set_timer_callback_period(5);

    while (1);
}



#ifdef CALC_CRC8
// CRC-8 calculation using poly x^8 + x^7 + x^6 + x^4 + x^2 + x^0 (0x1d5).
static uint8_t crc8(uint8_t crc, const uint8_t *buf, uint8_t len) {
    uint8_t i;
    while (len--) {
        crc ^= *buf++;
        for (i = 0; i < 8; i++) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0xd5;
            else
                crc <<= 1;
        }
    }
    return crc;
}
#else  // CALC_CRC8
// CRC-8 lookup table using poly x^8 + x^7 + x^6 + x^4 + x^2 + x^0 (0x1d5).
// This table was generated using the crc8 function above.
static const uint8_t crc8_lut[256] = {
    0x00, 0xd5, 0x7f, 0xaa, 0xfe, 0x2b, 0x81, 0x54,
    0x29, 0xfc, 0x56, 0x83, 0xd7, 0x02, 0xa8, 0x7d,
    0x52, 0x87, 0x2d, 0xf8, 0xac, 0x79, 0xd3, 0x06,
    0x7b, 0xae, 0x04, 0xd1, 0x85, 0x50, 0xfa, 0x2f,
    0xa4, 0x71, 0xdb, 0x0e, 0x5a, 0x8f, 0x25, 0xf0,
    0x8d, 0x58, 0xf2, 0x27, 0x73, 0xa6, 0x0c, 0xd9,
    0xf6, 0x23, 0x89, 0x5c, 0x08, 0xdd, 0x77, 0xa2,
    0xdf, 0x0a, 0xa0, 0x75, 0x21, 0xf4, 0x5e, 0x8b,
    0x9d, 0x48, 0xe2, 0x37, 0x63, 0xb6, 0x1c, 0xc9,
    0xb4, 0x61, 0xcb, 0x1e, 0x4a, 0x9f, 0x35, 0xe0,
    0xcf, 0x1a, 0xb0, 0x65, 0x31, 0xe4, 0x4e, 0x9b,
    0xe6, 0x33, 0x99, 0x4c, 0x18, 0xcd, 0x67, 0xb2,
    0x39, 0xec, 0x46, 0x93, 0xc7, 0x12, 0xb8, 0x6d,
    0x10, 0xc5, 0x6f, 0xba, 0xee, 0x3b, 0x91, 0x44,
    0x6b, 0xbe, 0x14, 0xc1, 0x95, 0x40, 0xea, 0x3f,
    0x42, 0x97, 0x3d, 0xe8, 0xbc, 0x69, 0xc3, 0x16,
    0xef, 0x3a, 0x90, 0x45, 0x11, 0xc4, 0x6e, 0xbb,
    0xc6, 0x13, 0xb9, 0x6c, 0x38, 0xed, 0x47, 0x92,
    0xbd, 0x68, 0xc2, 0x17, 0x43, 0x96, 0x3c, 0xe9,
    0x94, 0x41, 0xeb, 0x3e, 0x6a, 0xbf, 0x15, 0xc0,
    0x4b, 0x9e, 0x34, 0xe1, 0xb5, 0x60, 0xca, 0x1f,
    0x62, 0xb7, 0x1d, 0xc8, 0x9c, 0x49, 0xe3, 0x36,
    0x19, 0xcc, 0x66, 0xb3, 0xe7, 0x32, 0x98, 0x4d,
    0x30, 0xe5, 0x4f, 0x9a, 0xce, 0x1b, 0xb1, 0x64,
    0x72, 0xa7, 0x0d, 0xd8, 0x8c, 0x59, 0xf3, 0x26,
    0x5b, 0x8e, 0x24, 0xf1, 0xa5, 0x70, 0xda, 0x0f,
    0x20, 0xf5, 0x5f, 0x8a, 0xde, 0x0b, 0xa1, 0x74,
    0x09, 0xdc, 0x76, 0xa3, 0xf7, 0x22, 0x88, 0x5d,
    0xd6, 0x03, 0xa9, 0x7c, 0x28, 0xfd, 0x57, 0x82,
    0xff, 0x2a, 0x80, 0x55, 0x01, 0xd4, 0x7e, 0xab,
    0x84, 0x51, 0xfb, 0x2e, 0x7a, 0xaf, 0x05, 0xd0,
    0xad, 0x78, 0xd2, 0x07, 0x53, 0x86, 0x2c, 0xf9
};

static uint8_t crc8(uint8_t crc, const uint8_t *buf, uint8_t len) {
    while (len--) {
        crc ^= *buf++;
        crc = crc8_lut[crc];
    }
    return crc;
}
#endif  // CALC_CRC8

// Calculate the CRC8 for buf + 1 -> buf + len and place in buf[0].
static void i2c_message_calc_crc8(uint8_t reg, uint8_t *buf, uint8_t len) {
    // CRC init value is the base value (0xff) + register address.
    buf[0] = crc8(I2C_CRC8_BASE_VALUE + reg, buf + 1, len - 1);
}

// Verify the CRC8 for buf + 1 -> buf + len with the CRC8 in buf[0].
static uint8_t i2c_message_verify_crc8(uint8_t reg, const uint8_t *buf,
        uint8_t len) {
    // CRC init value is the base value (0xff) + register address.
    return crc8(I2C_CRC8_BASE_VALUE + reg, buf + 1, len - 1) == buf[0];
}


// Event interrupt callbacks.
// Called when a debounced rising or falling edge is detected on any button.
void button_state_changed(DEMO_BTN btn, BTN_STATE state) {
    // DEMO_BTN enum and button index are the same.
    switch (state) {
    case BTN_STATE_DOWN:
        live_state.buttons[btn].down_count++;
        break;

    case BTN_STATE_UP:
        live_state.buttons[btn].up_count++;
        break;

    default:
        return;
    }

    stm_set_data_ready(DATA_READY_YES);
    live_state_data = 1;
}

// Called when a debounced rising or falling edge is detected on the power
// source switch.
void pwr_source_state_changed(PWR_SOURCE_STATE state) {
    live_state.pwr_source = state;
    stm_set_data_ready(DATA_READY_YES);
    live_state_data = 1;
}

// Periodic timer callback, with the period set by
// stm_set_timer_callback_period.
void timer_callback(void) {
    static uint8_t next_value = 255;
    uint8_t i;

    // This callback will alternating blink the red LED and RGB led.
    stm_set_led(DEMO_LED_RED, next_value);

    next_value = next_value ? 0 : 255;

    for (i = 1; i < __DEMO_LED_COUNT; i++) {
        stm_set_led((DEMO_LED)i, next_value);
    }
}


// I2C interrupt callbacks.
// Called when the master is reading from the I2C_REG_READ_ACK register.
// The I2C module will DMA the data from the returned pointer and must remain
// valid until the transaction is complete.
const uint8_t *i2c_send_ack(void) {
    static const uint8_t ack[2] = {
        I2C_SEND_ACK_CHECKSUM,
        I2C_ACK
    };
    static const uint8_t nack[2] = {0x00, 0x00};

    const uint8_t *ptr = send_ack ? ack : nack;

    // Only send one ack per good message.  This prevent the master from
    // being able to send a message to an ignored register and getting
    // an ack.
    send_ack = 0;

    return ptr;
}

// Called when the master is reading from the I2C_REG_READ_SYSTEM_INFO
// register.  The I2C module will DMA the data from the returned pointer and
// must remain valid until the transaction is complete.
const DEMO_SYSTEM_INFO *i2c_send_system_info(void) {
    static DEMO_SYSTEM_INFO system_info = {
        0,
        {
            I2C_SYS_INFO_SLAVE_ENDIAN,
            I2C_SYS_INFO_PROTOCOL_VERSION,
            I2C_SYS_INFO_BOARD_ID_DEMO_BOARD_1,
            I2C_SYS_INFO_DEMO_BOARD_REV,
            I2C_SYS_INFO_CHIP_ID_STM8L152C6
        }
    };

    i2c_message_calc_crc8(I2C_REG_READ_SYSTEM_INFO,
            (uint8_t *)&system_info,
            sizeof(system_info));

    return &system_info;
}

// Called when the master is reading from the I2C_REG_READ_SYSTEM_STATE
// register.  The I2C module will DMA the data from the returned pointer and
// must remain valid until the transaction is complete.
const DEMO_SYSTEM_STATE *i2c_send_system_state(void) {
    uint8_t i;

    // If waiting for an ack from master do not copy live state to
    // latched state.  This prevents messages from being dropped
    // if the previous read failed.
    if (!waiting_for_ack) {
        for (i = 0; i < __DEMO_BTN_COUNT; i++) {
            // Copy each button state and clear the live state.
            latched_state.buttons[i].down_count =
                    live_state.buttons[i].down_count;
            latched_state.buttons[i].up_count =
                    live_state.buttons[i].up_count;
            live_state.buttons[i].down_count = 0;
            live_state.buttons[i].up_count = 0;
        }
        // Do not clear pwr_source.
        latched_state.pwr_source = live_state.pwr_source;

        latched_state.seq = slave_seq;
        i2c_message_calc_crc8(I2C_REG_READ_SYSTEM_STATE,
                (uint8_t *)&latched_state,
                sizeof(latched_state));

        waiting_for_ack = 1;
        live_state_data = 0;
    }

    return &latched_state;
}

// Called when the master is writing to the I2C_REG_WRITE_ACK register.  The
// pointer is only valid during this call.
void i2c_recv_ack(const uint8_t *ack) {
    if (ack[0] == I2C_RECV_ACK_CHECKSUM
            && ack[1] == I2C_ACK
            && waiting_for_ack == 1) {
        slave_seq++;
        waiting_for_ack = 0;
        if (!live_state_data) {
            stm_set_data_ready(DATA_READY_NO);
        }
    }
}

// Called when the master is writing to the I2C_REG_WRITE_SYSTEM_LED register.
// The pointer is only valid during this call.
void i2c_recv_system_led(const DEMO_LED_STATE *led_state) {
    uint8_t i;

    if (i2c_message_verify_crc8(I2C_REG_WRITE_SYSTEM_LED,
            (uint8_t *)led_state, sizeof(DEMO_LED_STATE))) {
        if (led_state->seq == master_seq) {
            // Still ack the beep module even if it does not have control of
            // the LEDs.
            if (beep_module_led_control) {
                for (i = 0; i < __DEMO_LED_COUNT; i++) {
                    stm_set_led((DEMO_LED)i, led_state->led_brightness[i]);
                }
            }
            master_seq++;
            send_ack = 1;
            return;
        } else if (led_state->seq == master_seq - 1) {
            send_ack = 1;
            return;
        }
    }
    send_ack = 0;
}

// Called when the master is writing to the I2C_REG_WRITE_BEEP_MODULE_MODE
// register.  The pointer is only valid during this call.
void i2c_recv_beep_module_mode(const DEMO_BEEP_MODULE_MODE *mode) {
    if (i2c_message_verify_crc8(I2C_REG_WRITE_BEEP_MODULE_MODE,
            (uint8_t *)mode, sizeof(DEMO_BEEP_MODULE_MODE))) {
        if (mode->seq == master_seq) {
            switch (mode->mode) {
            case I2C_BEEP_MODULE_MODE_NORMAL:
                stm_set_data_ready(DATA_READY_YES);
                live_state_data = 1;

            case I2C_BEEP_MODULE_MODE_SETUP:
                beep_module_led_control = 1;
                stm_set_timer_callback_period(0);
                break;

            case I2C_BEEP_MODULE_MODE_UPDATING:
                beep_module_led_control = 0;
                stm_set_timer_callback_period(3);
                break;

            default:
                break;
            }

            master_seq++;
            send_ack = 1;
            return;
        } else if (mode->seq == master_seq - 1) {
            send_ack = 1;
            return;
        }
    }
    send_ack = 0;
}

// Called when the master is writing to the I2C_REG_WRITE_SYNC register.  The
// pointer is only valid during this call.  This is the only register that
// does not use a CRC.
void i2c_recv_sync(const uint8_t *data) {
    if (data[0] == I2C_SYNC_KEY_0
            && data[1] == I2C_SYNC_KEY_1
            && data[2] == I2C_SYNC_KEY_2
            && data[3] == I2C_SYNC_KEY_3) {
        master_seq = 0;
        slave_seq = 0;
        send_ack = 0;
        waiting_for_ack = 0;
    }
}

#ifdef  USE_FULL_ASSERT
// Used only during development.
void assert_failed(uint8_t *file, uint32_t line) {
    while (1);
}
#endif
