#ifndef IOMCU_DEMO_STM_H
#define IOMCU_DEMO_STM_H

#include "stm8l15x.h"

#include "demo.h"


// General stm defines.
#define GPIO_HIGH(PORT, PINS)           (PORT)->ODR |= (PINS)
#define GPIO_LOW(PORT, PINS)            (PORT)->ODR &= ~(PINS)
#define GPIO_TOGGLE(PORT, PINS)         (PORT)->ODR ^= (PINS)
#define GPIO_STATE(PORT, PINS)          ((PORT)->IDR & (PINS))
#define GPIO_STATE_HIGH(PORT, PINS)     ((((PORT)->IDR & (PINS)) == (PINS)) ? 1 : 0)
#define GPIO_STATE_LOW(PORT, PINS)      ((!((PORT)->IDR & (PINS))) ? 1 : 0)


// Button stm defines.
#define BTN_GET_STATE(PORT, PIN) \
        (((PORT)->IDR & (PIN)) ? BTN_STATE_UP : BTN_STATE_DOWN)

// Used to simulate ac batt state of demo.
#define DEMO_AC_BATT_STATE_PORT                     (GPIOD)
#define DEMO_AC_BATT_STATE_PIN                      (GPIO_Pin_3)

#define DEMO_BTN_VOL_DOWN_PORT                      (GPIOB)
#define DEMO_BTN_VOL_DOWN_PIN                       (GPIO_Pin_1)
#define DEMO_BTN_VOL_UP_PORT                        (GPIOB)
#define DEMO_BTN_VOL_UP_PIN                         (GPIO_Pin_2)
#define DEMO_BTN_BACK_PORT                          (GPIOB)
#define DEMO_BTN_BACK_PIN                           (GPIO_Pin_3)
#define DEMO_BTN_PLAY_PORT                          (GPIOB)
#define DEMO_BTN_PLAY_PIN                           (GPIO_Pin_4)
#define DEMO_BTN_SKIP_PORT                          (GPIOB)
#define DEMO_BTN_SKIP_PIN                           (GPIO_Pin_5)
#define DEMO_BTN_MAGIC_PORT                         (GPIOB)
#define DEMO_BTN_MAGIC_PIN                          (GPIO_Pin_6)
#define DEMO_BTN_SPARE_PORT                         (GPIOB)
#define DEMO_BTN_SPARE_PIN                          (GPIO_Pin_7)

#define DEMO_BTN_COMMON_PORT                        (GPIOB)
#define DEMO_BTN_MODE                               (GPIO_Mode_In_PU_No_IT)


// LED stm defines.
#define DEMO_LED_RED_PORT                           (GPIOB)
#define DEMO_LED_RED_PIN                            (GPIO_Pin_0)
#define DEMO_LED_RGB_R_PORT                         (GPIOD)
#define DEMO_LED_RGB_R_PIN                          (GPIO_Pin_2)
#define DEMO_LED_RGB_G_PORT                         (GPIOD)
#define DEMO_LED_RGB_G_PIN                          (GPIO_Pin_4)
#define DEMO_LED_RGB_B_PORT                         (GPIOD)
#define DEMO_LED_RGB_B_PIN                          (GPIO_Pin_5)

#define DEMO_LED_MODE                               (GPIO_Mode_Out_PP_Low_Fast)


// I2C data ready defines.
#define I2C_DATA_READY_PORT                         (GPIOF)
#define I2C_DATA_READY_PIN                          (GPIO_Pin_0)
#define I2C_DATA_READY_MODE                         (GPIO_Mode_Out_PP_Low_Fast)


// GPIO state defines.
// state[7:7] - Last reported state
// state[1:0] - Last recorded state
// If both bits of last recorded state are different than reported state
// a debounced edge has been detected.
#define GPIO_STATE_INIT(init) \
        (init ? 0x83 : 0x00)
#define GPIO_STATE_LSHIFT(state) \
        (state = ((state & 0x80) | ((state << 1) & 0x03)))
#define GPIO_STATE_RISE_EDGE                        (0x03)
#define GPIO_STATE_FALL_EDGE                        (0x80)

extern uint8_t button_states[__DEMO_BTN_COUNT];
extern uint8_t pwr_source_state;


// I2C defines.
#define I2C_NORM_SPEED                              (100000)
#define I2C_DMA_PERIPHERAL_ADDR                     ((uint16_t)(0x5216))

#define I2C_RECV_BUF_SIZE                           (20)
#define I2C_SEND_BUF_SIZE                           (20)

extern uint8_t i2c_dma_recv_buf[I2C_RECV_BUF_SIZE];
extern uint8_t i2c_dma_send_buf[I2C_SEND_BUF_SIZE];

// Timer callback defines.
extern uint16_t timer_callback_deci_seconds;
extern uint16_t timer_callback_counter;


#endif  // IOMCU_DEMO_STM_H
