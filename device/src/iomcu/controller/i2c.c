#include "stm8l15x.h"

#include "controller.h"


uint8_t i2c_dma_recv_buf[I2C_RECV_BUF_SIZE];
uint8_t i2c_restart_key_buf[I2C_REG_WRITE_RESTART_KEY_SIZE];
uint8_t i2c_restart_key_state;

// enter and exit standalone mode registers both point to this
uint8_t i2c_standalone_mode_buf[I2C_REG_WRITE_STANDALONE_BUF_SIZE];
uint8_t i2c_standalone_mode_state;

uint8_t i2c_dma_send_buf[I2C_SEND_BUF_SIZE];
uint8_t i2c_read_knob_buf[I2C_REG_READ_KNOB_STAT_SIZE];
uint8_t i2c_read_knob_state;


const I2CDmaParams i2c_dma_read_params[I2C_REG_READ_COUNT] = {
    {
        .ct.size = I2C_REG_READ_SYS_STAT_SIZE,
        .ct.addr = i2c_dma_send_buf
    },
    {
        .ct.size = I2C_REG_READ_KNOB_STAT_SIZE,
        .ct.addr = i2c_read_knob_buf
#ifdef OPTICAL_ENCODER_DEBUG
    },
    {
        .ct.size = I2C_REG_READ_ADC_DATA_0_SIZE,
        .ct.addr = &knob_psense_dma_buf[0]
    },
    {
        .ct.size = I2C_REG_READ_ADC_DATA_1_SIZE,
        .ct.addr = &knob_psense_dma_buf[120]
    },
    {
        .ct.size = I2C_REG_READ_ADC_DATA_0R_SIZE,
        .ct.addr = &table_0_ready
    },
    {
        .ct.size = I2C_REG_READ_ADC_DATA_1R_SIZE,
        .ct.addr = &table_1_ready
    },
    {
        .ct.size = I2C_REG_READ_ADC_COMP_0_SIZE,
        .ct.addr = &knob_psense_dma_comp_buf[0]
    },
    {
        .ct.size = I2C_REG_READ_ADC_COMP_1_SIZE,
        .ct.addr = &knob_psense_dma_comp_buf[90]
    },
    {
        .ct.size = I2C_REG_READ_ADC_COMP_0R_SIZE,
        .ct.addr = &compressed_table_0_ready
    },
    {
        .ct.size = I2C_REG_READ_ADC_COMP_1R_SIZE,
        .ct.addr = &compressed_table_1_ready
    },
    {
        .ct.size = I2C_REG_READ_ENCODER_SIZE,
        .ct.addr = i2c_dma_send_buf
#endif  // OPTICAL_ENCODER_DEBUG
    }
};

const I2CDmaParams i2c_dma_write_params[I2C_REG_WRITE_COUNT] = {
    {
        .ct.size = I2C_REG_WRITE_PWM_SIZE,
        .ct.addr = LED_TABLE_PWM_A
    },
    {
        .ct.size = I2C_REG_WRITE_RESTART_KEY_SIZE,
        .ct.addr = i2c_restart_key_buf
    },
    {
        .ct.size = I2C_REG_WRITE_STANDALONE_BUF_SIZE,
        .ct.addr = i2c_standalone_mode_buf
    },
    {
        .ct.size = I2C_REG_WRITE_STANDALONE_BUF_SIZE,
        .ct.addr = i2c_standalone_mode_buf
    }
    
};

// CRC8 init is 0xff + register address 0x01.
#define I2C_UPREAD_READ_KNOB_INIT                   (0)

static uint8_t crc8(uint8_t crc, uint8_t *buf, uint8_t len) {
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

void i2c_update_read_knob_buf(void) {
    if (I2C_READ_KNOB_BUF_READY & i2c_read_knob_state) {
        return;
    }
    i2c_read_knob_buf[0] = (uint8_t)knob_encoder_read_count();
    i2c_read_knob_buf[1] = knob_switch_read_down_count();
    i2c_read_knob_buf[2] = knob_switch_read_up_count();
    i2c_read_knob_buf[3] = dropped_adc_frames;
    dropped_adc_frames -= i2c_read_knob_buf[3];

    if (i2c_read_knob_buf[0] || i2c_read_knob_buf[1]
            || i2c_read_knob_buf[2] || i2c_read_knob_buf[3]) {
        i2c_read_knob_buf[4] = crc8(0, i2c_read_knob_buf, 4);
    } else {
        i2c_read_knob_buf[4] = 0;
    }
    i2c_read_knob_state |= I2C_READ_KNOB_BUF_READY;
}
