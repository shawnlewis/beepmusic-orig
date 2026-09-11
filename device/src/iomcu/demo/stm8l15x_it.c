#include "stm8l15x.h"

#include "stm.h"
#include "iomcu_shared.h"

INTERRUPT_HANDLER(NonHandledInterrupt, 0) {
    while (1);
}

INTERRUPT_HANDLER_TRAP(TRAP_IRQHandler) {
    while (1);
}

INTERRUPT_HANDLER(FLASH_IRQHandler, 1) {
    while (1);
}

#define I2C_DMA_STATE_WAIT_FOR_REG                  (1<<0)
// Used for I2C_REG_RW_WRITE
#define I2C_DMA_STATE_RECEIVING                     (1<<1)
// Used for I2C_REG_RW_READ
#define I2C_DMA_STATE_TRANSMITTING                  (1<<2)

#define I2C_REG_RW_MASK                             (0x80)
#define I2C_REG_RW_READ                             (0x00)
#define I2C_REG_RW_WRITE                            (0x80)

#define I2C_DEMO_RESTART_KEY_0                      (IOMCU_COMMON_KEY_0)
#define I2C_DEMO_RESTART_KEY_1                      (IOMCU_COMMON_KEY_1)
#define I2C_DEMO_RESTART_KEY_2                      (IOMCU_COMMON_KEY_2)
#define I2C_DEMO_RESTART_KEY_3                      (IOMCU_COMMON_KEY_3)
#define I2C_DEMO_RESTART_KEY_CHECKSUM               (IOMCU_COMMON_KEY_CRC8_CHECKSUM)

uint8_t i2c_dma_recv_buf[I2C_RECV_BUF_SIZE];
uint8_t i2c_dma_send_buf[I2C_SEND_BUF_SIZE];
static uint8_t i2c_reg;

static uint8_t i2c_dma_state = I2C_DMA_STATE_WAIT_FOR_REG;

#define DMA_ADDR_CM0ARH(ADDR) \
    ((uint8_t)(((uint16_t)(ADDR)) >> (uint8_t)8))
#define DMA_ADDR_CM0ARL(ADDR) \
    ((uint8_t)(((uint16_t)(ADDR)) & 0xff))

INTERRUPT_HANDLER(DMA1_CHANNEL0_1_IRQHandler, 2) {
    uint8_t *ptr;
    uint8_t size;

    switch (i2c_dma_state) {
    case I2C_DMA_STATE_WAIT_FOR_REG: {
        i2c_reg = i2c_dma_recv_buf[0];

        // Validate register and setup any data needed.
        switch (i2c_reg) {
        case I2C_REG_READ_ACK:
            ptr = (uint8_t *)i2c_send_ack();
            size = 2;
            break;

        case I2C_REG_READ_SYSTEM_INFO:
            ptr = (uint8_t *)i2c_send_system_info();
            size = 13;
            break;

        case I2C_REG_READ_SYSTEM_STATE:
            ptr = (uint8_t *)i2c_send_system_state();
            size = 17;
            break;

        case I2C_REG_WRITE_ACK:
            size = 2;
            break;

        case I2C_REG_WRITE_RESERVED:
            size = 5;
            break;

        case I2C_REG_WRITE_SYSTEM_LED:
            size = 6;
            break;

        case I2C_REG_WRITE_BEEP_MODULE_MODE:
            size = 3;
            break;

        case I2C_REG_WRITE_SYNC:
            size = 4;
            break;

        default:
            i2c_reg = I2C_REG_RESERVED;
            break;
        }

        // If register was valid setup dma params.
        if (i2c_reg != I2C_REG_RESERVED) {
            if ((i2c_reg & I2C_REG_RW_MASK) == I2C_REG_RW_WRITE) {
                DMA1_Channel0->CCR &= ~DMA_CCR_CE;
                DMA1_Channel0->CNBTR = size;
                DMA1_Channel0->CM0ARH = DMA_ADDR_CM0ARH(i2c_dma_recv_buf + 1);
                DMA1_Channel0->CM0ARL = DMA_ADDR_CM0ARL(i2c_dma_recv_buf + 1);
                DMA1_Channel0->CCR |= DMA_CCR_CE;
                i2c_dma_state = I2C_DMA_STATE_RECEIVING;
            } else {
                DMA1_Channel3->CCR &= ~DMA_CCR_CE;
                DMA1_Channel3->CNBTR = size;
                DMA1_Channel3->CM0ARH = DMA_ADDR_CM0ARH(ptr);
                DMA1_Channel3->CM0ARL = DMA_ADDR_CM0ARL(ptr);
                DMA1_Channel3->CCR |= DMA_CCR_CE;
                i2c_dma_state = I2C_DMA_STATE_TRANSMITTING;
            }

        }

        break;
    }

    case I2C_DMA_STATE_RECEIVING:
        DMA1_Channel0->CCR &= ~DMA_CCR_CE;
        DMA1_Channel0->CNBTR = 1;
        DMA1_Channel0->CM0ARH = DMA_ADDR_CM0ARH(i2c_dma_recv_buf);
        DMA1_Channel0->CM0ARL = DMA_ADDR_CM0ARL(i2c_dma_recv_buf);
        DMA1_Channel0->CCR |= DMA_CCR_CE;
        i2c_dma_state = I2C_DMA_STATE_WAIT_FOR_REG;

        switch (i2c_reg) {
        case I2C_REG_WRITE_ACK:
            i2c_recv_ack((uint8_t *)(i2c_dma_recv_buf + 1));
            break;

        case I2C_REG_WRITE_RESERVED:
            if (i2c_dma_recv_buf[1] == I2C_DEMO_RESTART_KEY_3
                    && i2c_dma_recv_buf[2] == I2C_DEMO_RESTART_KEY_2
                    && i2c_dma_recv_buf[3] == I2C_DEMO_RESTART_KEY_1
                    && i2c_dma_recv_buf[4] == I2C_DEMO_RESTART_KEY_0
                    && i2c_dma_recv_buf[5] == I2C_DEMO_RESTART_KEY_CHECKSUM) {
                WWDG->CR = WWDG_CR_WDGA;  // Generate a watchdog reset.
            }
            break;

        case I2C_REG_WRITE_SYSTEM_LED:
            i2c_recv_system_led((DEMO_LED_STATE *)(i2c_dma_recv_buf + 1));
            break;

        case I2C_REG_WRITE_BEEP_MODULE_MODE:
            i2c_recv_beep_module_mode(
                    (DEMO_BEEP_MODULE_MODE *)(i2c_dma_recv_buf + 1));
            break;

        case I2C_REG_WRITE_SYNC:
            i2c_recv_sync((uint8_t *)(i2c_dma_recv_buf + 1));
            break;

        default:
            break;
        }

        i2c_reg = I2C_REG_RESERVED;
        break;

    default:
        i2c_dma_state = I2C_DMA_STATE_WAIT_FOR_REG;
        break;
    }

    DMA_ClearITPendingBit(DMA1_IT_TC0);
}

INTERRUPT_HANDLER(DMA1_CHANNEL2_3_IRQHandler, 3) {
    switch (i2c_dma_state) {
    case I2C_DMA_STATE_TRANSMITTING:
        DMA1_Channel3->CCR &= ~DMA_CCR_CE;
        DMA1_Channel3->CNBTR = 1;
        DMA1_Channel3->CM0ARH = DMA_ADDR_CM0ARH(i2c_dma_send_buf);
        DMA1_Channel3->CM0ARL = DMA_ADDR_CM0ARL(i2c_dma_send_buf);
        DMA1_Channel3->CCR |= DMA_CCR_CE;
        i2c_dma_state = I2C_DMA_STATE_WAIT_FOR_REG;

        i2c_reg = I2C_REG_RESERVED;
        break;

    default:
        i2c_dma_state = I2C_DMA_STATE_WAIT_FOR_REG;
        break;
    }

    DMA_ClearITPendingBit(DMA1_IT_TC3);
}

INTERRUPT_HANDLER(RTC_CSSLSE_IRQHandler, 4) {
    while (1);
}

INTERRUPT_HANDLER(EXTIE_F_PVD_IRQHandler, 5) {
    while (1);
}

INTERRUPT_HANDLER(EXTIB_G_IRQHandler, 6) {
    while (1);
}

INTERRUPT_HANDLER(EXTID_H_IRQHandler, 7) {
    while (1);
}

INTERRUPT_HANDLER(EXTI0_IRQHandler, 8) {
    while (1);
}

INTERRUPT_HANDLER(EXTI1_IRQHandler, 9) {
    while (1);
}

INTERRUPT_HANDLER(EXTI2_IRQHandler, 10) {
    while (1);
}

INTERRUPT_HANDLER(EXTI3_IRQHandler, 11) {
    while (1);
}

INTERRUPT_HANDLER(EXTI4_IRQHandler, 12) {
    while (1);
}

INTERRUPT_HANDLER(EXTI5_IRQHandler, 13) {
    while (1);
}

INTERRUPT_HANDLER(EXTI6_IRQHandler, 14) {
    while (1);
}

INTERRUPT_HANDLER(EXTI7_IRQHandler, 15) {
    while (1);
}

INTERRUPT_HANDLER(LCD_AES_IRQHandler, 16) {
    while (1);
}

INTERRUPT_HANDLER(SWITCH_CSS_BREAK_DAC_IRQHandler, 17) {
    while (1);
}

INTERRUPT_HANDLER(ADC1_COMP_IRQHandler, 18) {
    while (1);
}

INTERRUPT_HANDLER(TIM2_UPD_OVF_TRG_BRK_USART2_TX_IRQHandler, 19) {
    while (1);
}

INTERRUPT_HANDLER(TIM2_CC_USART2_RX_IRQHandler, 20) {
    while (1);
}

uint16_t timer_callback_deci_seconds;
uint16_t timer_callback_counter;

INTERRUPT_HANDLER(TIM3_UPD_OVF_TRG_BRK_USART3_TX_IRQHandler, 21) {
    timer_callback_counter++;
    if ((timer_callback_deci_seconds != 0) &&
        (timer_callback_counter == timer_callback_deci_seconds)) {
        timer_callback_counter = 0;
        timer_callback();
    }

    TIM3_ClearFlag(TIM3_FLAG_Update);
}

INTERRUPT_HANDLER(TIM3_CC_USART3_RX_IRQHandler, 22) {
    while (1);
}

INTERRUPT_HANDLER(TIM1_UPD_OVF_TRG_COM_IRQHandler, 23) {
    while (1);
}

INTERRUPT_HANDLER(TIM1_CC_IRQHandler, 24) {
    while (1);
}

uint8_t button_states[__DEMO_BTN_COUNT];
uint8_t pwr_source_state;

INTERRUPT_HANDLER(TIM4_UPD_OVF_TRG_IRQHandler, 25) {
    uint8_t i;
    // Button index 0 starts at GPIO B1.
    uint8_t pin = GPIO_Pin_1;

    for (i = 0; i < __DEMO_BTN_COUNT; i++) {
        GPIO_STATE_LSHIFT(button_states[i]);
        button_states[i] |= GPIO_STATE_HIGH(DEMO_BTN_COMMON_PORT, pin);
        if (button_states[i] == GPIO_STATE_RISE_EDGE) {
            button_state_changed((DEMO_BTN)i, BTN_STATE_UP);
            button_states[i] ^= 0x80;
        } else if (button_states[i] == GPIO_STATE_FALL_EDGE) {
            button_state_changed((DEMO_BTN)i, BTN_STATE_DOWN);
            button_states[i] ^= 0x80;
        }
        pin <<= 1;
    }

    GPIO_STATE_LSHIFT(pwr_source_state);
    pwr_source_state |= GPIO_STATE_HIGH(DEMO_AC_BATT_STATE_PORT,
            DEMO_AC_BATT_STATE_PIN);
    if (pwr_source_state == GPIO_STATE_RISE_EDGE) {
        pwr_source_state_changed(PWR_SOURCE_STATE_BATT);
        pwr_source_state ^= 0x80;
    } else if (pwr_source_state == GPIO_STATE_FALL_EDGE) {
        pwr_source_state_changed(PWR_SOURCE_STATE_AC);
        pwr_source_state ^= 0x80;
    }

    // Clear TIM4 update interrupt flag.
    TIM4->SR1 = (uint8_t)(~(uint8_t)TIM4_IT_Update);
}

INTERRUPT_HANDLER(SPI1_IRQHandler, 26) {
    while (1);
}

INTERRUPT_HANDLER(USART1_TX_TIM5_UPD_OVF_TRG_BRK_IRQHandler, 27) {
    while (1);
}

INTERRUPT_HANDLER(USART1_RX_TIM5_CC_IRQHandler, 28) {
    while (1);
}

static uint8_t sr1;
static uint8_t sr2;
// Needs to be read but never used, adding static will cause a compiler error.
uint8_t sr3;

INTERRUPT_HANDLER(I2C1_SPI2_IRQHandler, 29) {
    // The bulk of the i2c data handling is done by dma.

    // save the I2C registers configuration.
    sr1 = I2C1->SR1;
    sr2 = I2C1->SR2;
    sr3 = I2C1->SR3;

    // Comm error.
    if (sr2 & (I2C_SR2_WUFH | I2C_SR2_OVR |I2C_SR2_ARLO |I2C_SR2_BERR)) {
        I2C1->CR2 |= I2C_CR2_STOP;  // stop communication - release the lines.
        I2C1->SR2 = 0;  // clear all error flags.
    }

    // ACK failure.
    if (sr2 & I2C_SR2_AF) {
        I2C1->SR2 &= ~I2C_SR2_AF;  // clear AF.
        //i2c_message_stop();
    }
    // Stop bit from master.
    if (sr1 & I2C_SR1_STOPF) {
        I2C1->CR2 |= I2C_CR2_ACK;  // CR2 write to clear STOPF.
        //i2c_message_stop();
    }
}
