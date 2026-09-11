#ifndef IOMCU_CONTROLLER_H
#define IOMCU_CONTROLLER_H

#include "app_controller_proto.h"


// Option defines.
// Use this for strobing PB0-5, comment out for strobing PD0-3.
//#define LED_ROW_COL_SWAP
// Use this for 10 bit + 1 PWM (0 - 1024), comment out for 8 bit
// (raw ARR values).
//#define LED_10_BIT_PWM
// This uses 3 LedArrayData tables instead of 4 but the pwm table will be
// overwritten after each update.
//#define LED_TABLE_REUSE
// Use this for starting with D11 at full PWM for debugging.
//#define LED_D11_BOOT
// :)
#define LED_SPLASH_BOOT

extern uint8_t startup_mode;
void enter_startup_mode(void);
void exit_startup_mode(void);
void draw_startup_pattern(void);
#define STARTUP_TIMER_SECOND            (61)
#define STARTUP_MODE_ENABLE             (1<<0)
#define LED_STARTUP_BLINK               (3)
#define LED_BLINK_SEQUENCE_NUM          (5)
extern uint8_t led_startup_blink_ontime[LED_BLINK_SEQUENCE_NUM];
extern uint8_t led_startup_blink_offtime[LED_BLINK_SEQUENCE_NUM];

// Use these to disable various funcionality.
//#define KNOB_DISABLE
//#define I2C_DISABLE
//#define LED_DISABLE

// Use this to enable optical encoder debug (disables everything
// else and enables the raw encoder data read out).
//#define OPTICAL_ENCODER_DEBUG
#ifdef OPTICAL_ENCODER_DEBUG
// Ensure KNOB is enabled.
#ifdef KNOB_DISABLE
#undef KNOB_DISABLE
#endif  // KNOB_DISABLE

// Ensure I2C is enabled.
#ifdef I2C_DISABLE
#undef I2C_DISABLE
#endif  // I2C_DISABLE

// Disable LEDs.
#ifndef LED_DISABLE
#define LED_DISABLE
#endif  // LED_DISABLE

#endif  // OPTICAL_ENCODER_DEBUG

// General defines.
#define GPIO_HIGH(PORT, PINS)           (PORT)->ODR |= (PINS)
#define GPIO_LOW(PORT, PINS)            (PORT)->ODR &= ~(PINS)
#define GPIO_TOGGLE(PORT, PINS)         (PORT)->ODR ^= (PINS)
#define GPIO_STATE(PORT, PINS)          ((PORT)->IDR & (PINS))
#define GPIO_STATE_HIGH(PORT, PINS)     ((((PORT)->IDR & (PINS)) == (PINS)) ? 1 : 0)
#define GPIO_STATE_LOW(PORT, PINS)      ((!((PORT)->IDR & (PINS))) ? 1 : 0)

#define DMA_ADDR_CM0ARH(ADDR) \
    ((uint8_t)(((uint16_t)(ADDR)) >> (uint8_t)8))
#define DMA_ADDR_CM0ARL(ADDR) \
    ((uint8_t)(((uint16_t)(ADDR)) & 0xff))


// Spare defines.
#define GPIO_SPARE_0_PORT               (GPIOC)
#define GPIO_SPARE_1_PORT               (GPIOC)
#define GPIO_SPARE_2_PORT               (GPIOC)
#define GPIO_SPARE_3_PORT               (GPIOB)
#define GPIO_SPARE_0_PIN                (GPIO_Pin_4)
#define GPIO_SPARE_1_PIN                (GPIO_Pin_5)
#define GPIO_SPARE_2_PIN                (GPIO_Pin_6)
#define GPIO_SPARE_3_PIN                (GPIO_Pin_6)


// LED array defines.
#ifndef LED_ROW_COL_SWAP

#define LED_ROW_SIZE                    (4)
#define LED_ROW_PORT                    (GPIOD)
#define LED_ROW_0                       (GPIO_Pin_0)
#define LED_ROW_1                       (GPIO_Pin_1)
#define LED_ROW_2                       (GPIO_Pin_2)
#define LED_ROW_3                       (GPIO_Pin_3)
#define LED_ROW_ALL \
    (LED_ROW_0 | LED_ROW_1 | LED_ROW_2 | LED_ROW_3)
#define LED_COL_SIZE                    (6)
#define LED_COL_PORT                    (GPIOB)
#define LED_COL_0                       (GPIO_Pin_0)
#define LED_COL_1                       (GPIO_Pin_1)
#define LED_COL_2                       (GPIO_Pin_2)
#define LED_COL_3                       (GPIO_Pin_3)
#define LED_COL_4                       (GPIO_Pin_4)
#define LED_COL_5                       (GPIO_Pin_5)
#define LED_COL_ALL \
    (LED_COL_0 | LED_COL_1 | LED_COL_2 | LED_COL_3 | LED_COL_4 | LED_COL_5)

#define LED_ROW_ON(ROWS)                GPIO_HIGH(LED_ROW_PORT, (ROWS))
#define LED_ROW_OFF(ROWS)               GPIO_LOW(LED_ROW_PORT, (ROWS))
#define LED_COL_ON(COLS)                GPIO_LOW(LED_COL_PORT, (COLS))
#define LED_COL_OFF(COLS)               GPIO_HIGH(LED_COL_PORT, (COLS))
#define LED_ROW_GPIO_INIT_MODE          (GPIO_Mode_Out_PP_Low_Fast)
#define LED_COL_GPIO_INIT_MODE          (GPIO_Mode_Out_PP_High_Fast)

#else  // LED_ROW_COL_SWAP

#define LED_ROW_SIZE                    (6)
#define LED_ROW_PORT                    (GPIOB)
#define LED_ROW_0                       (GPIO_Pin_0)
#define LED_ROW_1                       (GPIO_Pin_1)
#define LED_ROW_2                       (GPIO_Pin_2)
#define LED_ROW_3                       (GPIO_Pin_3)
#define LED_ROW_4                       (GPIO_Pin_4)
#define LED_ROW_5                       (GPIO_Pin_5)
#define LED_ROW_ALL \
    (LED_ROW_0 | LED_ROW_1 | LED_ROW_2 | LED_ROW_3 | LED_ROW_4 | LED_ROW_5)
#define LED_COL_SIZE                    (4)
#define LED_COL_PORT                    (GPIOD)
#define LED_COL_0                       (GPIO_Pin_0)
#define LED_COL_1                       (GPIO_Pin_1)
#define LED_COL_2                       (GPIO_Pin_2)
#define LED_COL_3                       (GPIO_Pin_3)
#define LED_COL_ALL \
    (LED_COL_0 | LED_COL_1 | LED_COL_2 | LED_COL_3)

#define LED_ROW_ON(ROWS)                GPIO_LOW(LED_ROW_PORT, (ROWS))
#define LED_ROW_OFF(ROWS)               GPIO_HIGH(LED_ROW_PORT, (ROWS))
#define LED_COL_ON(COLS)                GPIO_HIGH(LED_COL_PORT, (COLS))
#define LED_COL_OFF(COLS)               GPIO_LOW(LED_COL_PORT, (COLS))
#define LED_ROW_GPIO_INIT_MODE          (GPIO_Mode_Out_PP_High_Fast)
#define LED_COL_GPIO_INIT_MODE          (GPIO_Mode_Out_PP_Low_Fast)

#endif  // LED_ROW_COL_SWAP

#define LED_ROW_TOGGLE(ROWS)            GPIO_TOGGLE(LED_ROW_PORT, (ROWS))
#define LED_COL_TOGGLE(COLS)            GPIO_TOGGLE(LED_COL_PORT, (ROWS))

#define LED_TOT_SIZE                    (LED_ROW_SIZE * LED_COL_SIZE)
#define LED_OP_COUNT                    (LED_ROW_SIZE * (LED_COL_SIZE + 1))
#define LED_OP_COL_MASK                 (LED_COL_ALL)
#define LED_OP_ROW_INC                  (1<<6)
#define LED_OP_LOOP                     (1<<7)

#ifdef LED_10_BIT_PWM
typedef uint16_t LedArrayPWM;
#define LED_ARRAY_SPARE                 (1)
#else  // LED_10_BIT_PWM
typedef uint8_t LedArrayPWM;
#define LED_ARRAY_SPARE                 (1)
#endif  // LED_10_BIT_PWM

extern uint8_t led_startup_lut[LED_TOT_SIZE];

typedef struct {
    uint8_t arr;
    uint8_t op;
} LedArrayOp;

typedef struct {
    uint8_t idx[LED_TOT_SIZE];
    uint8_t arr[LED_TOT_SIZE];
    uint8_t rarr[LED_ROW_SIZE];
} LedArrayCalc;

// Spare bytes are added to the PWM table for extra data sent
// with I2C_REG_WRITE_PWM (i.e. ACK/NACK for previous read).
// This is required since the DMA needs contiguous data.
typedef union {
    LedArrayPWM pwm[LED_TOT_SIZE + LED_ARRAY_SPARE];
    LedArrayOp ops[LED_OP_COUNT];
    LedArrayCalc calc;
} LedArrayData;

#define LED_STATE_PWM_READY             (1<<0)
#define LED_STATE_OPS_READY             (1<<1)
#define LED_STATE_OFF_TIME              (1<<2)

#define LED_OFF_TIME_ARR                (25)

#define LED_TABLE_CALC                  (&led_data[0].calc)
#define LED_TABLE_PWM_A                 (led_data[1].pwm)

#ifdef LED_TABLE_REUSE
#error "LED_TABLE_REUSE is broken"
// LED_TABLE_REUSE is broken because the i2c dma pointers are not updated
// during a table swap.  It cannot be used when i2c params are const.
#define LED_TABLE_COUNT                 (3)
#define LED_TABLE_PWM_B                 (led_data[2].pwm)
#define LED_TABLE_OPS_A                 (led_data[1].ops)
#define LED_TABLE_OPS_B                 (led_data[2].ops)

// This is to be called only by TIM4_UPD_OVF_TRG_IRQHandler when
// PWM_READY and OPS_READY are set.
#define LED_TABLE_OPS_SWAP() \
    do { \
        led_pwm_table = (led_pwm_table == LED_TABLE_PWM_A) ? \
                LED_TABLE_PWM_B : LED_TABLE_PWM_A; \
        led_ops_table = (led_ops_table == LED_TABLE_OPS_A) ? \
                LED_TABLE_OPS_B : LED_TABLE_OPS_A; \
        led_state = 0; \
    } while (0)
#else
#define LED_TABLE_COUNT                 (4)
#define LED_TABLE_OPS_A                 (led_data[2].ops)
#define LED_TABLE_OPS_B                 (led_data[3].ops)

// This is to be called only by TIM4_UPD_OVF_TRG_IRQHandler when
// PWM_READY and OPS_READY are set.
#define LED_TABLE_OPS_SWAP() \
    do { \
        led_ops_table = (led_ops_table == LED_TABLE_OPS_A) ? \
                LED_TABLE_OPS_B : LED_TABLE_OPS_A; \
        led_state = 0; \
    } while (0)
#endif  // LED_TABLE_REUSE

extern LedArrayData led_data[LED_TABLE_COUNT];
extern uint8_t led_state;
extern LedArrayCalc *led_calc_table;
extern LedArrayPWM *led_pwm_table;
extern LedArrayOp *led_ops_table;

// Used in stm8l15x_it.c.
extern LedArrayOp *led_current_op;

void led_data_init(void);
void led_update_arr(void);


// TODO: Move common i2c defines to iomcu/common/*.h
// I2C defines.
#define I2C_DMA_PERIPHERAL_ADDR         ((uint16_t)(0x5216))

// Read/write is from the stm8l.
#define I2C_REG_RW_MASK                 (0x80)
#define I2C_REG_RW_READ                 (0x00)
#define I2C_REG_RW_WRITE                (0x80)
#define I2C_REG_READ_SYS_STAT           (0x00)
#define I2C_REG_READ_SYS_STAT_SIZE      (3)
#define I2C_REG_READ_KNOB_STAT          (0x01)
#define I2C_REG_READ_KNOB_STAT_SIZE     (5)
#define I2C_REG_READ_COUNT              (2)
#define I2C_REG_WRITE_PWM               (0x80)
#define I2C_REG_WRITE_PWM_SIZE          (24 + LED_ARRAY_SPARE)
#define I2C_REG_WRITE_RESTART_KEY       (0x81)
#define I2C_REG_ENTER_STANDALONE_MODE   (0x82)
#define I2C_REG_EXIT_STANDALONE_MODE    (0x83)
// 4 key bytes + 1 checksum byte.
#define I2C_REG_WRITE_RESTART_KEY_SIZE  (5)
#define I2C_REG_WRITE_STANDALONE_BUF_SIZE (4)
#define I2C_REG_WRITE_COUNT             (4)
#define I2C_REG_RESERVED                (0xff)

// Add extra i2c registers for encoder debug.  Keep these inline
// with base registers.
#ifdef OPTICAL_ENCODER_DEBUG
#define I2C_REG_READ_ADC_DATA_0         (0x02)
#define I2C_REG_READ_ADC_DATA_0_SIZE    (120)
#define I2C_REG_READ_ADC_DATA_1         (0x03)
#define I2C_REG_READ_ADC_DATA_1_SIZE    (120)
#define I2C_REG_READ_ADC_DATA_0R        (0x04)
#define I2C_REG_READ_ADC_DATA_0R_SIZE   (1)
#define I2C_REG_READ_ADC_DATA_1R        (0x05)
#define I2C_REG_READ_ADC_DATA_1R_SIZE   (1)
#define I2C_REG_READ_ADC_COMP_0         (0x06)
#define I2C_REG_READ_ADC_COMP_0_SIZE    (90)
#define I2C_REG_READ_ADC_COMP_1         (0x07)
#define I2C_REG_READ_ADC_COMP_1_SIZE    (90)
#define I2C_REG_READ_ADC_COMP_0R        (0x08)
#define I2C_REG_READ_ADC_COMP_0R_SIZE   (1)
#define I2C_REG_READ_ADC_COMP_1R        (0x09)
#define I2C_REG_READ_ADC_COMP_1R_SIZE   (1)
#define I2C_REG_READ_ENCODER            (0x0a)
#define I2C_REG_READ_ENCODER_SIZE       (1)

#undef I2C_REG_READ_COUNT
#define I2C_REG_READ_COUNT              (11)
#endif  // OPTICAL_ENCODER_DEBUG

#define I2C_INT_PORT                    (GPIOD)
#define I2C_INT_PIN                     (GPIO_Pin_4)

#define I2C_RECV_BUF_SIZE               (1)
#define I2C_SEND_BUF_SIZE               (4)

#define I2C_RESTART_KEY_READY           (1<<0)
#define I2C_STANDALONE_MODE_READY       (1<<0)
#define I2C_READ_KNOB_BUF_READY         (1<<0)
#define I2C_READ_KNOB_ACK               (0xaa)

// The IAR compiler can not handle the DMA_ADDR_CM0ARH/L macros
// at compile time.  This is a way to define the dma params as a const
// and still use them easily during runtime.
typedef union {
    struct {
        uint8_t size;
        uint8_t addrh;
        uint8_t addrl;
    } rt;
    struct {
        uint8_t size;
        uint8_t *addr;
    } ct;
} I2CDmaParams;

extern uint8_t i2c_dma_recv_buf[I2C_RECV_BUF_SIZE];
extern uint8_t i2c_restart_key_buf[I2C_REG_WRITE_RESTART_KEY_SIZE];
extern uint8_t i2c_restart_key_state;
extern uint8_t i2c_standalone_mode_buf[I2C_REG_WRITE_STANDALONE_BUF_SIZE];
extern uint8_t i2c_standalone_mode_state;
extern uint8_t i2c_dma_send_buf[I2C_SEND_BUF_SIZE];
extern uint8_t i2c_read_knob_buf[I2C_REG_READ_KNOB_STAT_SIZE];
extern uint8_t i2c_read_knob_state;

extern const I2CDmaParams i2c_dma_read_params[I2C_REG_READ_COUNT];
extern const I2CDmaParams i2c_dma_write_params[I2C_REG_WRITE_COUNT];

void i2c_update_read_knob_buf(void);

// Standalone mode defines
#define STANDALONE_MODE_UPDATING	(0)

// Knob defines.
#define KNOB_SW_PORT                    (GPIOB)
#define KNOB_PSENSE_0_PORT              (GPIOA)
#define KNOB_PSENSE_1_PORT              (GPIOA)
#define KNOB_SW_PIN                     (GPIO_Pin_7)
#define KNOB_PSENSE_0_PIN               (GPIO_Pin_4)
#define KNOB_PSENSE_1_PIN               (GPIO_Pin_5)

#define KNOB_PSENSE_DMA_CH              (DMA1_Channel1)
#define KNOB_PSENSE_DMA_SYSCFG_REMAP    (REMAP_DMA1Channel_ADC1ToChannel1)
#define KNOB_PSENSE_DMA_BUF_SIZE        (240)
#define KNOB_PSENSE_DMA_COMP_BUF_SIZE   (180)
#define KNOB_PSENSE_COMP_BLOCKS         (60)
// Use this for > 8bit ADC values.
//#define KNOB_PSENSE_DMA_PERIPHERAL_ADDR ((uint16_t)0x5344)
#define KNOB_PSENSE_DMA_PERIPHERAL_ADDR ((uint16_t)0x5345)

#define KNOB_PSENSE_0_ADC1_CH           (ADC_Channel_2)
#define KNOB_PSENSE_1_ADC1_CH           (ADC_Channel_1)
#define KNOB_PSENSE_ADC1_GROUP          (ADC_Group_SlowChannels)

// Setup ARR for TIM2, used to poll knob push button.
#define TIM2_BOOT_ARR                   (1023)


extern uint8_t knob_psense_active_table;
extern uint8_t knob_psense_dma_buf[KNOB_PSENSE_DMA_BUF_SIZE];
extern uint8_t adc_proc_state;
extern uint8_t table_0_ready;
extern uint8_t table_1_ready;

void knob_switch_read(void);
uint8_t knob_switch_read_down_count(void);
uint8_t knob_switch_read_up_count(void);
void knob_encoder_proc_data(uint8_t half);
int8_t knob_encoder_read_count(void);

#ifdef OPTICAL_ENCODER_DEBUG
extern uint8_t compressed_table_0_ready;
extern uint8_t compressed_table_1_ready;
extern uint8_t knob_psense_dma_comp_buf[KNOB_PSENSE_DMA_COMP_BUF_SIZE];

void knob_encoder_compress_data(uint8_t half);
#endif  // OPTICAL_ENCODER_DEBUG

// standalone mode
void update_standalone_leds(void);


// IT defines.
extern uint8_t dropped_adc_frames;

#endif  // IOMCU_CONTROLLER_H
