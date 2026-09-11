#include "stm8l15x.h"

#include "stm.h"


void stm_hw_init(void) {
    // Set system clock to 16MHz internal.
    CLK_SYSCLKDivConfig(CLK_SYSCLKDiv_1);
    CLK_SYSCLKSourceSwitchCmd(ENABLE);
    CLK_SYSCLKSourceConfig(CLK_SYSCLKSource_HSI);
    while (CLK_GetSYSCLKSource() != CLK_SYSCLKSource_HSI);


    // Setup button GPIO pins which for simplicity are mapped to each EXTI pin.
    GPIO_Init(DEMO_AC_BATT_STATE_PORT, DEMO_AC_BATT_STATE_PIN, DEMO_BTN_MODE);
    GPIO_Init(DEMO_BTN_VOL_DOWN_PORT, DEMO_BTN_VOL_DOWN_PIN, DEMO_BTN_MODE);
    GPIO_Init(DEMO_BTN_VOL_UP_PORT, DEMO_BTN_VOL_UP_PIN, DEMO_BTN_MODE);
    GPIO_Init(DEMO_BTN_BACK_PORT, DEMO_BTN_BACK_PIN, DEMO_BTN_MODE);
    GPIO_Init(DEMO_BTN_PLAY_PORT, DEMO_BTN_PLAY_PIN, DEMO_BTN_MODE);
    GPIO_Init(DEMO_BTN_SKIP_PORT, DEMO_BTN_SKIP_PIN, DEMO_BTN_MODE);
    GPIO_Init(DEMO_BTN_MAGIC_PORT, DEMO_BTN_MAGIC_PIN, DEMO_BTN_MODE);
    GPIO_Init(DEMO_BTN_SPARE_PORT, DEMO_BTN_SPARE_PIN, DEMO_BTN_MODE);


    // Setup LED GPIO pins.
    GPIO_Init(DEMO_LED_RED_PORT, DEMO_LED_RED_PIN, DEMO_LED_MODE);
    GPIO_Init(DEMO_LED_RGB_R_PORT, DEMO_LED_RGB_R_PIN, DEMO_LED_MODE);
    GPIO_Init(DEMO_LED_RGB_G_PORT, DEMO_LED_RGB_G_PIN, DEMO_LED_MODE);
    GPIO_Init(DEMO_LED_RGB_B_PORT, DEMO_LED_RGB_B_PIN, DEMO_LED_MODE);


    // Setup I2C data ready pin.
    GPIO_Init(I2C_DATA_READY_PORT, I2C_DATA_READY_PIN, I2C_DATA_READY_MODE);
}

void stm_hw_start(void) {
    uint8_t i;
    uint8_t pin;

    // Enable TIM1 clock.
    CLK_PeripheralClockConfig(CLK_Peripheral_TIM1, ENABLE);

    // Setup TIM1 counting clock: 16MHz / (63 + 1) = 250kHz.
    // TIM1 period for PWM frequency: 250kHz / (254 + 1) = 980Hz.
    TIM1_TimeBaseInit(63, TIM1_CounterMode_Up, 254, 0);

    // Setup TIM1 output channel 1 for GPIO D2 (LED_RGB_R pin).
    // Set initial duty cycle to 0.
    TIM1_OC1Init(
            TIM1_OCMode_PWM1,
            TIM1_OutputState_Enable,
            TIM1_OutputNState_Disable,
            0,
            TIM1_OCPolarity_High,
            TIM1_OCNPolarity_Low,
            TIM1_OCIdleState_Set,
            TIM1_OCNIdleState_Set);
    TIM1_OC1PreloadConfig(ENABLE);

    // Setup TIM1 output channel 2 for GPIO D4 (LED_RGB_G pin).
    // Set initial duty cycle to 0.
    TIM1_OC2Init(
            TIM1_OCMode_PWM1,
            TIM1_OutputState_Enable,
            TIM1_OutputNState_Disable,
            0,
            TIM1_OCPolarity_High,
            TIM1_OCNPolarity_Low,
            TIM1_OCIdleState_Set,
            TIM1_OCNIdleState_Set);
    TIM1_OC2PreloadConfig(ENABLE);

    // Setup TIM1 output channel 3 for GPIO D5 (LED_RGB_G pin).
    // Set initial duty cycle to 0.
    TIM1_OC3Init(
            TIM1_OCMode_PWM1,
            TIM1_OutputState_Enable,
            TIM1_OutputNState_Disable,
            0,
            TIM1_OCPolarity_High,
            TIM1_OCNPolarity_Low,
            TIM1_OCIdleState_Set,
            TIM1_OCNIdleState_Set);
    TIM1_OC3PreloadConfig(ENABLE);

    // Enable ARR preload, PWM output and start TIM1.
    TIM1_ARRPreloadConfig(ENABLE);
    TIM1_CtrlPWMOutputs(ENABLE);
    TIM1_Cmd(ENABLE);


    // Enable TIM2 clock.
    CLK_PeripheralClockConfig(CLK_Peripheral_TIM2, ENABLE);

    // Setup TIM2 counting clock: 16MHz / (64) = 250kHz.
    // TIM2 period for PWM frequency: 250kHz / (254 + 1) = 980Hz.
    TIM2_TimeBaseInit(TIM2_Prescaler_64, TIM2_CounterMode_Up, 254);

    // Setup TIM2 output channel 1 for GPIO B0 (LED_RED pin).
    // Set initial duty cycle to 0.
    TIM2_OC1Init(
            TIM2_OCMode_PWM1,
            TIM2_OutputState_Enable,
            0,
            TIM2_OCPolarity_High,
            TIM2_OCIdleState_Set);
    TIM2_OC1PreloadConfig(ENABLE);

    // Enable ARR preload, PWM output and start TIM2.
    TIM2_ARRPreloadConfig(ENABLE);
    TIM2_CtrlPWMOutputs(ENABLE);
    TIM2_Cmd(ENABLE);


    // Enabled TIM3 clock for programmable sw timer callback.
    CLK_PeripheralClockConfig(CLK_Peripheral_TIM3, ENABLE);

    // Setup TIM3 periodic clock: 16MHz / 128 / (12499 + 1) = 10Hz.
    //                                               period = 0.1s.
    TIM3_TimeBaseInit(TIM3_Prescaler_128, TIM3_CounterMode_Up, 12499);

    // Enable TIM3 update interrupt but do not start TIM3 until it is needed.
    TIM3_ITConfig(TIM3_IT_Update, ENABLE);


    // Enable TIM4 clock to poll the button GPIOs.
    CLK_PeripheralClockConfig(CLK_Peripheral_TIM4, ENABLE);

    // Setup TIM4 periodic clock: 16Mhz / 128 / (255 + 1) = 488Hz
    //                                             period = 2.048ms
    TIM4_TimeBaseInit(TIM4_Prescaler_128, 255);

    // Enable TIM4 update interrupt.
    TIM4_ITConfig(TIM4_IT_Update, ENABLE);

    // Start TIM4
    TIM4_Cmd(ENABLE);


    // Enable I2C clock.
    CLK_PeripheralClockConfig(CLK_Peripheral_I2C1, ENABLE);

    // Reset I2C parameters which may have been setup by bootloader.
    I2C_DeInit(I2C1);

    // Setup I2C paramters, frequency and duty cycle are not used in slave mode.
    I2C_Init(I2C1, I2C_NORM_SPEED, (I2C_DEMO_ADDR << 1),
            I2C_Mode_I2C, I2C_DutyCycle_2, I2C_Ack_Enable,
            I2C_AcknowledgedAddress_7bit);

    // Enable minimal interrupts required for DMA mode.
    I2C_ITConfig(I2C1, (I2C_IT_TypeDef)(I2C_IT_ERR | I2C_IT_EVT),
            ENABLE);

    // Enable DMA clocks.
    CLK_PeripheralClockConfig(CLK_Peripheral_DMA1, ENABLE);

    // Setup DMA channel0 for I2C receive and channel1 for I2C transmit.
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

    // Enable DMA transfer complete interrupts.
    DMA_ITConfig(DMA1_Channel0, DMA_ITx_TC, ENABLE);
    DMA_ITConfig(DMA1_Channel3, DMA_ITx_TC, ENABLE);

    // Enable DMA channels.
    DMA_Cmd(DMA1_Channel0, ENABLE);
    DMA_Cmd(DMA1_Channel3, ENABLE);
    DMA_GlobalCmd(ENABLE);

    // Start DMA.
    I2C_DMACmd(I2C1, ENABLE);

    // Start I2C.
    I2C_Cmd(I2C1, ENABLE);


    // Right before we start interrupts load the current state of each
    // GPIO for debouncing in the TIM4 interrupt handlers.
    // Button index 0 starts at pin 1.
    pin = GPIO_Pin_1;
    for (i = 0; i < __DEMO_BTN_COUNT; i++) {
        button_states[i] = GPIO_STATE_INIT(
                GPIO_HIGH(DEMO_BTN_COMMON_PORT, pin));
        pin <<= 1;
    }
    pwr_source_state = GPIO_STATE_INIT(
            GPIO_STATE_HIGH(DEMO_AC_BATT_STATE_PORT,
            DEMO_AC_BATT_STATE_PIN));

    // Enable interrupts and everything is running.
    enableInterrupts();
}

void stm_set_led(DEMO_LED led, uint8_t value) {
    // All of these LEDs are 8-bit PWM capable, don't need to check value at
    // all.
    switch (led) {
    case DEMO_LED_RED:
        TIM2_SetCompare1((uint16_t)value);
        break;

    case DEMO_LED_RGB_R:
        TIM1_SetCompare1((uint16_t)value);
        break;

    case DEMO_LED_RGB_G:
        TIM1_SetCompare2((uint16_t)value);
        break;

    case DEMO_LED_RGB_B:
        TIM1_SetCompare3((uint16_t)value);
        break;

    default:
        break;
    }
}

uint8_t stm_get_led(DEMO_LED led) {
    // There is no GetCompare so just read the raw register.  Since we're
    // only using 8-bit we can just always return the lower byte.
    switch (led) {
    case DEMO_LED_RED:
        return TIM2->CCR1L;

    case DEMO_LED_RGB_R:
        return TIM1->CCR1L;

    case DEMO_LED_RGB_G:
        return TIM1->CCR2L;

    case DEMO_LED_RGB_B:
        return TIM1->CCR3L;

    default:
        return 0;
    }
}

void stm_set_data_ready(DATA_READY ready) {
    switch (ready) {
    case DATA_READY_NO:
        GPIO_LOW(I2C_DATA_READY_PORT, I2C_DATA_READY_PIN);
        break;

    case DATA_READY_YES:
        GPIO_HIGH(I2C_DATA_READY_PORT, I2C_DATA_READY_PIN);
        break;

    default:
        break;
    }
}

DATA_READY stm_get_data_ready(void) {
    return GPIO_STATE(I2C_DATA_READY_PORT, I2C_DATA_READY_PIN) ?
                DATA_READY_YES : DATA_READY_NO;
}

BTN_STATE stm_get_button_state(DEMO_BTN btn) {
    switch (btn) {
    case DEMO_BTN_VOL_DOWN:
        return BTN_GET_STATE(
                DEMO_BTN_VOL_DOWN_PORT,
                DEMO_BTN_VOL_DOWN_PIN);

    case DEMO_BTN_VOL_UP:
        return BTN_GET_STATE(
                DEMO_BTN_VOL_UP_PORT,
                DEMO_BTN_VOL_UP_PIN);

    case DEMO_BTN_BACK:
        return BTN_GET_STATE(
                DEMO_BTN_BACK_PORT,
                DEMO_BTN_BACK_PIN);

    case DEMO_BTN_PLAY:
        return BTN_GET_STATE(
                DEMO_BTN_PLAY_PORT,
                DEMO_BTN_PLAY_PIN);

    case DEMO_BTN_SKIP:
        return BTN_GET_STATE(
                DEMO_BTN_SKIP_PORT,
                DEMO_BTN_SKIP_PIN);

    case DEMO_BTN_MAGIC:
        return BTN_GET_STATE(
                DEMO_BTN_MAGIC_PORT,
                DEMO_BTN_MAGIC_PIN);

    case DEMO_BTN_SPARE:
        return BTN_GET_STATE(
                DEMO_BTN_SPARE_PORT,
                DEMO_BTN_SPARE_PIN);

    default:
        return BTN_INVALID;
    }
}

PWR_SOURCE_STATE stm_get_power_source_state(void) {
    return (PWR_SOURCE_STATE)BTN_GET_STATE(
            DEMO_AC_BATT_STATE_PORT,
            DEMO_AC_BATT_STATE_PIN);
}

void stm_set_timer_callback_period(uint16_t deci_seconds) {
    timer_callback_deci_seconds = deci_seconds;

    if (deci_seconds) {
        // Disable TIM3.
        TIM3_Cmd(DISABLE);

        // Set current counter values back to 0.
        TIM3_SetCounter(0);

        // Clear interrupt flags.
        TIM3_ClearFlag(TIM3_FLAG_Update);
        TIM3_ClearFlag(TIM3_FLAG_Trigger);

        // Reset timer counter.
        timer_callback_counter = 0;

        // (Re)enable TIM3.
        TIM3_Cmd(ENABLE);
    } else {
        // Disable TIM3.
        TIM3_Cmd(DISABLE);

        // Clear interrupt flags.
        TIM3_ClearFlag(TIM3_FLAG_Update);
    }
}
