#include "stm8l15x.h"

#include "controller.h"


INTERRUPT_HANDLER(NonHandledInterrupt, 0) {
    while (1);
}

INTERRUPT_HANDLER_TRAP(TRAP_IRQHandler) {
    while (1);
}

INTERRUPT_HANDLER(FLASH_IRQHandler, 1) {
    while (1);
}

#define I2C_DMA_STATE_WAIT_FOR_REG  (1<<0)
// Used for I2C_REG_RW_WRITE
#define I2C_DMA_STATE_RECEIVING     (1<<1)
// Used for I2C_REG_RW_READ
#define I2C_DMA_STATE_TRANSMITTING  (1<<2)

static uint8_t i2c_dma_state = I2C_DMA_STATE_WAIT_FOR_REG;
static uint8_t i2c_reg;
uint8_t dropped_adc_frames;

INTERRUPT_HANDLER(DMA1_CHANNEL0_1_IRQHandler, 2) {
    if (DMA_GetITStatus(DMA1_IT_HT1)) {
#ifdef OPTICAL_ENCODER_DEBUG
        table_1_ready = 0;
        table_0_ready = 1;
#endif  // OPTICAL_ENCODER_DEBUG
        if (!adc_proc_state) {
            adc_proc_state = 1;
        } else {
            dropped_adc_frames++;
        }
        DMA_ClearITPendingBit(DMA1_IT_HT1);
    } else if (DMA_GetITStatus(DMA1_IT_TC1)) {
#ifdef OPTICAL_ENCODER_DEBUG
        table_0_ready = 0;
        table_1_ready = 1;
#endif  // OPTICAL_ENCODER_DEBUG
        if (!adc_proc_state) {
            adc_proc_state = 2;
        } else {
            dropped_adc_frames++;
        }
        DMA_ClearITPendingBit(DMA1_IT_TC1);
    } else {
        switch (i2c_dma_state) {
        case I2C_DMA_STATE_WAIT_FOR_REG: {
            i2c_reg = i2c_dma_recv_buf[0];

            // Validate register and setup any data needed.
            switch (i2c_reg) {
            case I2C_REG_READ_SYS_STAT:
                // Used by devio2 to know this is using the updated i2c
                // protocol with knob switch up and down.
                i2c_dma_send_buf[0] = 0;
                i2c_dma_send_buf[1] = 0;
                i2c_dma_send_buf[2] = 1;
                break;

            case I2C_REG_READ_KNOB_STAT:
                i2c_update_read_knob_buf();
                break;

#ifdef OPTICAL_ENCODER_DEBUG
            case I2C_REG_READ_ENCODER:
                i2c_dma_send_buf[0] = (uint8_t)knob_encoder_read_count();
                break;

            case I2C_REG_READ_ADC_DATA_0:
            case I2C_REG_READ_ADC_DATA_1:
            case I2C_REG_READ_ADC_DATA_0R:
            case I2C_REG_READ_ADC_DATA_1R:
            case I2C_REG_READ_ADC_COMP_0:
            case I2C_REG_READ_ADC_COMP_1:
            case I2C_REG_READ_ADC_COMP_0R:
            case I2C_REG_READ_ADC_COMP_1R:
                break;
#endif  // OPTICAL_ENCODER_DEBUG

            case I2C_REG_WRITE_RESTART_KEY:
            case I2C_REG_WRITE_PWM:
            case I2C_REG_ENTER_STANDALONE_MODE:
            case I2C_REG_EXIT_STANDALONE_MODE:
                break;
                
            default:
                i2c_reg = I2C_REG_RESERVED;
                break;
            }

            // If register was valid setup dma params.
            if (i2c_reg != I2C_REG_RESERVED) {
                uint8_t offset;
                DMA_Channel_TypeDef *dma_chan;
                const I2CDmaParams *dma_params;

                if ((i2c_reg & I2C_REG_RW_MASK) == I2C_REG_RW_WRITE) {
                    offset = i2c_reg & ~I2C_REG_RW_MASK;
                    dma_chan = DMA1_Channel0;
                    dma_params = i2c_dma_write_params;
                    i2c_dma_state = I2C_DMA_STATE_RECEIVING;
                } else {
                    offset = i2c_reg;
                    dma_chan = DMA1_Channel3;
                    dma_params = i2c_dma_read_params;
                    i2c_dma_state = I2C_DMA_STATE_TRANSMITTING;
                }
                
                dma_chan->CCR &= ~DMA_CCR_CE;
                dma_chan->CNBTR = dma_params[offset].rt.size;
                dma_chan->CM0ARH = dma_params[offset].rt.addrh;
                dma_chan->CM0ARL = dma_params[offset].rt.addrl;
                dma_chan->CCR |= DMA_CCR_CE;
            }

            break;
        }

        case I2C_DMA_STATE_RECEIVING:
          
          // exit both startup or standalone mode on i2c traffic
          if( (startup_mode & STARTUP_MODE_ENABLE) == STARTUP_MODE_ENABLE)
          {
              startup_mode &= ~STARTUP_MODE_ENABLE;
          }
          if((i2c_standalone_mode_state & I2C_STANDALONE_MODE_READY) == \
            I2C_STANDALONE_MODE_READY)
          {
            i2c_standalone_mode_state &= ~I2C_STANDALONE_MODE_READY;
            TIM3_ITConfig(TIM3_IT_Update, DISABLE);
            TIM3_Cmd(DISABLE);
          }

          if(i2c_reg == I2C_REG_ENTER_STANDALONE_MODE)
          {
              i2c_standalone_mode_state |= I2C_STANDALONE_MODE_READY;
              TIM3_Cmd(ENABLE);
              TIM3_ITConfig(TIM3_IT_Update, ENABLE);
          }
          if (i2c_reg == I2C_REG_EXIT_STANDALONE_MODE)
          {
              i2c_standalone_mode_state &= ~I2C_STANDALONE_MODE_READY;
              TIM3_ITConfig(TIM3_IT_Update, DISABLE);
              TIM3_Cmd(DISABLE);
          }
          
          DMA1_Channel0->CCR &= ~DMA_CCR_CE;
          DMA1_Channel0->CNBTR = 1;
          DMA1_Channel0->CM0ARH = DMA_ADDR_CM0ARH(i2c_dma_recv_buf);
          DMA1_Channel0->CM0ARL = DMA_ADDR_CM0ARL(i2c_dma_recv_buf);
          DMA1_Channel0->CCR |= DMA_CCR_CE;
          i2c_dma_state = I2C_DMA_STATE_WAIT_FOR_REG;
            
          // TODO: Add triggers to dma params.
          if (i2c_reg == I2C_REG_WRITE_PWM && ((i2c_standalone_mode_state & \
            I2C_STANDALONE_MODE_READY) != I2C_STANDALONE_MODE_READY)) {
              led_state |= LED_STATE_PWM_READY;
              if (LED_TABLE_PWM_A[LED_TOT_SIZE] == I2C_READ_KNOB_ACK) {
                  i2c_read_knob_state &= ~I2C_READ_KNOB_BUF_READY;
              }
          } else if (i2c_reg == I2C_REG_WRITE_RESTART_KEY) {
              i2c_restart_key_state |= I2C_RESTART_KEY_READY;
          }

          i2c_reg = I2C_REG_RESERVED;
          break;

        default:
            i2c_dma_state = I2C_DMA_STATE_WAIT_FOR_REG;
            break;
        }

        DMA_ClearITPendingBit(DMA1_IT_TC0);
    }
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
    // Only polling knob switch.
    knob_switch_read();
    TIM2->SR1 = (uint8_t)(~(uint8_t)TIM2_IT_Update);
}

INTERRUPT_HANDLER(TIM2_CC_USART2_RX_IRQHandler, 20) {
    while (1);
}

INTERRUPT_HANDLER(TIM3_UPD_OVF_TRG_BRK_USART3_TX_IRQHandler, 21) {
  
  static uint8_t standalone_animation_timer = 0;
  static uint16_t startup_animation_timer = 0;
  static uint16_t startup_animation_timeout = 3 * STARTUP_TIMER_SECOND;
  extern uint8_t quadrant;
  static uint8_t i = 0;
  
  if(((startup_mode & STARTUP_MODE_ENABLE) == STARTUP_MODE_ENABLE) \
        && (startup_animation_timer++ > startup_animation_timeout))
  {
    if(led_startup_lut[LED_STARTUP_BLINK] != 0)
    {
      led_startup_lut[LED_STARTUP_BLINK] = 0;
      startup_animation_timeout = led_startup_blink_offtime[i]; // blink off
    }else
    {
      led_startup_lut[LED_STARTUP_BLINK] = 255;
      startup_animation_timeout = led_startup_blink_ontime[i] * \
        STARTUP_TIMER_SECOND;
    }
    startup_animation_timer = 0;
    i+1 == LED_BLINK_SEQUENCE_NUM ? i = 0 : i++;
  }else if((standalone_animation_timer++ > 162) && ((i2c_standalone_mode_state \
    & I2C_STANDALONE_MODE_READY) == I2C_STANDALONE_MODE_READY))
  {//approx 1/3 second (2.048ms each)
    quadrant++;
    update_standalone_leds();
    standalone_animation_timer = 0;
  }
  
  TIM3->SR1 = (uint8_t)(~(uint8_t)TIM3_IT_Update);
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

uint8_t active_row = LED_ROW_0;
LedArrayOp *led_current_op;
uint8_t inactive_cols = 0;

#define RESTART_INTERRUPT_ARR       (5)
static const uint8_t burn_arr_table[5] = {18, 37, 55, 73, 91};
__IO uint8_t burn_counter;

INTERRUPT_HANDLER(TIM4_UPD_OVF_TRG_IRQHandler, 25) {
    LED_ROW_OFF(active_row);
    if (led_current_op->op & LED_OP_ROW_INC) {
        if (!(led_state & LED_STATE_OFF_TIME)) {
            LED_COL_OFF(LED_COL_ALL);
            led_state |= LED_STATE_OFF_TIME;

            TIM4->ARR = (led_current_op->arr < LED_OFF_TIME_ARR) ?
                    led_current_op->arr : LED_OFF_TIME_ARR;

            TIM4->SR1 = (uint8_t)(~(uint8_t)(TIM4_IT_Update | TIM4_IT_Trigger));
            return;
        }
        led_state &= ~LED_STATE_OFF_TIME;

        inactive_cols = 0;

        if (led_current_op->op & LED_OP_LOOP) {
            if ((led_state & (LED_STATE_PWM_READY | LED_STATE_OPS_READY))
                    == (LED_STATE_PWM_READY | LED_STATE_OPS_READY)) {
                LED_TABLE_OPS_SWAP();
                led_state = 0;
            }

            led_current_op = led_ops_table - 1;
            active_row = LED_ROW_0;
        } else {
            active_row <<= 1;
        }

        LED_COL_ON((~((led_current_op + 1)->op)) & LED_OP_COL_MASK);
    }

    led_current_op++;

    LED_COL_OFF(led_current_op->op & LED_OP_COL_MASK);

    inactive_cols |= led_current_op->op & LED_OP_COL_MASK;

    if (inactive_cols != LED_COL_ALL)
        LED_ROW_ON(active_row);

    if (led_current_op->arr <= RESTART_INTERRUPT_ARR) {
        burn_counter = burn_arr_table[led_current_op->arr - 1];
        while (burn_counter--);
        asm("pop ?b0");
        asm("jp TIM4_UPD_OVF_TRG_IRQHandler");
    }

    TIM4->ARR = led_current_op->arr;

    TIM4->SR1 = (uint8_t)(~(uint8_t)(TIM4_IT_Update | TIM4_IT_Trigger));
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
uint8_t sr3;

INTERRUPT_HANDLER(I2C1_SPI2_IRQHandler, 29) {
    // The bulk of the i2c data handling is done by dma.
    // Leaving the data handling in for now.

    // save the I2C registers configuration.
    sr1 = I2C1->SR1;
    sr2 = I2C1->SR2;
    sr3 = I2C1->SR3;

    // Comm error.
    if (sr2 & (I2C_SR2_WUFH | I2C_SR2_OVR |I2C_SR2_ARLO |I2C_SR2_BERR)) {
        I2C1->CR2 |= I2C_CR2_STOP;  // stop communication - release the lines.
        I2C1->SR2 = 0;  // clear all error flags.
    }
    //// Bytes recieved, byte transfer finished.
    //if ((sr1 & (I2C_SR1_RXNE | I2C_SR1_BTF)) ==
    //        (I2C_SR1_RXNE | I2C_SR1_BTF)) {
    //    i2c_byte_in(I2C1->DR);
    //}
    // Bytes recieved.
    //if (sr1 & I2C_SR1_RXNE) {
    //    i2c_byte_in(I2C1->DR);
    //}
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
    // Address matched.
    //if (sr1 & I2C_SR1_ADDR) {
    //    i2c_message_start(((sr3 & I2C_SR3_TRA) == I2C_SR3_TRA)
    //            ? I2C_START_TRA : I2C_START_RECV);
    //}
    //// Ready to send, byte transfer finished.
    //if ((sr1 & (I2C_SR1_TXE | I2C_SR1_BTF)) == (I2C_SR1_TXE | I2C_SR1_BTF)) {
    //    I2C1->DR = i2c_byte_out();
    //}
    //// Ready to send.
    //if (sr1 & I2C_SR1_TXE) {
    //    I2C1->DR = i2c_byte_out();
    //}
}
