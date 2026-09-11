#include "stm8l15x.h"

#include "controller.h"

// Using nothing.
void syscfg_init(void) {
    // Set clock source to 16Mhz internal.
    CLK_SYSCLKDivConfig(CLK_SYSCLKDiv_1);
    CLK_SYSCLKSourceSwitchCmd(ENABLE);
    CLK_SYSCLKSourceConfig(CLK_SYSCLKSource_HSI);
    while (CLK_GetSYSCLKSource() != CLK_SYSCLKSource_HSI);

    // Output sysclk / 1 to PC4.
    //CLK_CCOConfig(CLK_CCOSource_HSI, CLK_CCODiv_1);
}

// Using I2C1, PC0-1, DMA0,3.
void i2c_hw_init(void) {
    CLK_PeripheralClockConfig(CLK_Peripheral_I2C1, ENABLE);
    I2C_DeInit(I2C1);

    I2C_Init(I2C1, IOMCU_I2C_NORM_SPEED, (I2C_CONTROLLER_ADDR << 1),
            I2C_Mode_I2C, I2C_DutyCycle_2, I2C_Ack_Enable,
            I2C_AcknowledgedAddress_7bit);
    I2C_ITConfig(I2C1, (I2C_IT_TypeDef)(I2C_IT_ERR | I2C_IT_EVT),
            ENABLE);

    CLK_PeripheralClockConfig(CLK_Peripheral_DMA1, ENABLE);
    DMA_Init(DMA1_Channel0,
            ((uint16_t)(i2c_dma_recv_buf)),
            I2C_DMA_PERIPHERAL_ADDR,
            1,
            DMA_DIR_PeripheralToMemory,
            DMA_Mode_Circular,
            DMA_MemoryIncMode_Inc,
            DMA_Priority_Low,
            DMA_MemoryDataSize_Byte);
    DMA_Init(DMA1_Channel3,
            ((uint16_t)(i2c_dma_send_buf)),
            I2C_DMA_PERIPHERAL_ADDR,
            1,
            DMA_DIR_MemoryToPeripheral,
            DMA_Mode_Circular,
            DMA_MemoryIncMode_Inc,
            DMA_Priority_Low,
            DMA_MemoryDataSize_Byte);

    DMA_ITConfig(DMA1_Channel0, DMA_ITx_TC, ENABLE);
    DMA_ITConfig(DMA1_Channel3, DMA_ITx_TC, ENABLE);

    DMA_Cmd(DMA1_Channel0, ENABLE);
    DMA_Cmd(DMA1_Channel3, ENABLE);
    DMA_GlobalCmd(ENABLE);

    I2C_DMACmd(I2C1, ENABLE);

    I2C_Cmd(I2C1, ENABLE);
}

// Using TIM4, PD0-3, PB0-5
void led_hw_init(void) {
    GPIO_Init(LED_COL_PORT, LED_COL_ALL, LED_COL_GPIO_INIT_MODE);
    GPIO_Init(LED_ROW_PORT, LED_ROW_ALL, LED_ROW_GPIO_INIT_MODE);

    CLK_PeripheralClockConfig(CLK_Peripheral_TIM4, ENABLE);

    // Start timer condition all LEDs off, first timer should be
    // full row time.
    // 1 / (16e6 / 128 / (255 + 1)) = 2.048ms (488.28125Hz).
    TIM4_TimeBaseInit(TIM4_Prescaler_128, 255);

    TIM4_ClearFlag(TIM4_FLAG_Update);
    TIM4_Cmd(ENABLE);

    // Set LED priority highest.
    ITC_SetSoftwarePriority(TIM4_UPD_OVF_TRG_IRQn, ITC_PriorityLevel_3);

    TIM4_ITConfig((TIM4_IT_TypeDef)(TIM4_IT_Update | TIM4_IT_Trigger), ENABLE);
}

// Using TIM2.
void tim2_init(void) {
    CLK_PeripheralClockConfig(CLK_Peripheral_TIM2, ENABLE);

    TIM2_TimeBaseInit(TIM2_Prescaler_64, TIM2_CounterMode_Up, TIM2_BOOT_ARR);

    TIM2_ClearFlag(TIM2_FLAG_Update);
    TIM2_Cmd(ENABLE);
    TIM2_ITConfig(TIM2_IT_Update, ENABLE);
}

// Using TIM3.
void tim3_init(void) {
    CLK_PeripheralClockConfig(CLK_Peripheral_TIM3, ENABLE);

    // 1 / (16e6 / 128 / (255 + 1)) = 2.048ms (488.28125Hz).
    TIM3_TimeBaseInit(TIM3_Prescaler_128, TIM3_CounterMode_Up, 255);

    TIM3_ClearFlag(TIM3_FLAG_Update);
    TIM3_Cmd(ENABLE);
    TIM3_ITConfig(TIM3_IT_Update, ENABLE);
}

// Using PB7, PA4, PA5, ADC1, DMA1.
void knob_hw_init(void) {
    // Setup switch input with weak pullup.
    GPIO_Init(KNOB_SW_PORT, KNOB_SW_PIN, GPIO_Mode_In_PU_No_IT);

    CLK_PeripheralClockConfig(CLK_Peripheral_ADC1, ENABLE);

    // ADC conversion time = 1 / (16e6 / 2 / (384 + 8)) = 49uS
    //                                                  = 20408 s/S
    ADC_Init(ADC1, ADC_ConversionMode_Continuous, ADC_Resolution_8Bit,
            ADC_Prescaler_2);
    ADC_SamplingTimeConfig(ADC1, KNOB_PSENSE_ADC1_GROUP,
            ADC_SamplingTime_384Cycles);

    ADC_Cmd(ADC1, ENABLE);

    // Enable both PSENSE pins.
    ADC_ChannelCmd(ADC1, KNOB_PSENSE_0_ADC1_CH, ENABLE);
    ADC_ChannelCmd(ADC1, KNOB_PSENSE_1_ADC1_CH, ENABLE);

    // Map ADC to use DMA1
    SYSCFG_REMAPDMAChannelConfig(KNOB_PSENSE_DMA_SYSCFG_REMAP);

    CLK_PeripheralClockConfig(CLK_Peripheral_DMA1, ENABLE);

    // The ADC is set to continuous mode so it will always run.  The DMA
    // is setup for circular mode so it will read the buffer size and then
    // automatically reset its CNBTR register.  This ensures that every
    // ADC conversion is written by the DMA and will always be in the correct
    // slot in the ADC stream.
    DMA_Init(KNOB_PSENSE_DMA_CH,
            ((uint16_t)(knob_psense_dma_buf)),
            KNOB_PSENSE_DMA_PERIPHERAL_ADDR,
            KNOB_PSENSE_DMA_BUF_SIZE,
            DMA_DIR_PeripheralToMemory,
            DMA_Mode_Circular,
            DMA_MemoryIncMode_Inc,
            DMA_Priority_Low,
            DMA_MemoryDataSize_Byte);
    DMA_ITConfig(KNOB_PSENSE_DMA_CH, DMA_ITx_TC, ENABLE);
    DMA_ITConfig(KNOB_PSENSE_DMA_CH, DMA_ITx_HT, ENABLE);
    DMA_Cmd(KNOB_PSENSE_DMA_CH, ENABLE);

    DMA_GlobalCmd(ENABLE);

    ADC_DMACmd(ADC1, ENABLE);

    // Start ADC after DMA enabled to ensure the conversions are in the
    // correct slot in the ADC stream.
    ADC_SoftwareStartConv(ADC1);
}

void main(void) {
    syscfg_init();

#ifndef LED_DISABLE
    led_data_init();
    led_hw_init();
    tim3_init();
#endif  // LED_DISABLE

#ifndef KNOB_DISABLE
    knob_hw_init();
    tim2_init();
#endif // KNOB_DISABLE

#ifndef I2C_DISABLE
    i2c_hw_init();
#endif  // I2C_DISABLE

    enableInterrupts();
    
    enter_startup_mode();
    
    while((startup_mode & STARTUP_MODE_ENABLE) == STARTUP_MODE_ENABLE)
    {
        draw_startup_pattern();
        
        if ((led_state & (LED_STATE_PWM_READY | LED_STATE_OPS_READY))
        == LED_STATE_PWM_READY) {
#ifndef LED_DISABLE
            led_update_arr();
#else  // LED_DISABLE
            // If the LEDs are disabled I2C commands for the LEDs may still
            // be sent, reset the led_state as needed so the mcu will not
            // NACK the next command thinking it's busy.
            led_state = 0;
#endif  // LED_DISABLE
        }
      
    }
    exit_startup_mode();
    
    while (1) {
      
        if ((i2c_restart_key_state & I2C_RESTART_KEY_READY)
                == I2C_RESTART_KEY_READY) {
            i2c_restart_key_state &= ~I2C_RESTART_KEY_READY;
            // Keys are always sent MSB (KEY_3) first.
            if ((i2c_restart_key_buf[0] == I2C_CONTROLLER_RESTART_KEY_3)
                    && (i2c_restart_key_buf[1] == I2C_CONTROLLER_RESTART_KEY_2)
                    && (i2c_restart_key_buf[2] == I2C_CONTROLLER_RESTART_KEY_1)
                    && (i2c_restart_key_buf[3] == I2C_CONTROLLER_RESTART_KEY_0)
                    && (i2c_restart_key_buf[4]
                    == I2C_CONTROLLER_RESTART_KEY_CHECKSUM)) {
                WWDG->CR = WWDG_CR_WDGA;  // Generate a watchdog reset.
            }
        }

        if ((led_state & (LED_STATE_PWM_READY | LED_STATE_OPS_READY))
                == LED_STATE_PWM_READY) {
#ifndef LED_DISABLE
            led_update_arr();
#else  // LED_DISABLE
            // If the LEDs are disabled I2C commands for the LEDs may still
            // be sent, reset the led_state as needed so the mcu will not
            // NACK the next command thinking it's busy.
            led_state = 0;
#endif  // LED_DISABLE
        }

#ifndef KNOB_DISABLE
#ifndef OPTICAL_ENCODER_DEBUG
        if (adc_proc_state) {
            knob_encoder_proc_data(adc_proc_state >> 1);
            adc_proc_state = 0;
        }
#else  // OPTICAL_ENCODER_DEBUG
        if (adc_proc_state) {
            knob_encoder_compress_data(adc_proc_state >> 1);
            adc_proc_state = 0;
        }
#endif  // OPTICAL_ENCODER_DEBUG
#endif  // KNOB_DISABLE

    }
}

void assert_failed(uint8_t *file, uint32_t line) {
    while (1) {
    }
}
