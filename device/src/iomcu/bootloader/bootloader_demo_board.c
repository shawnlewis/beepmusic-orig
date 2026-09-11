#include "stm8l15x.h"

#include "bootloader.h"


// This is used to prevent the LEDs from glitching as much when the
// bootloader is starting.  These do not get reset before starting the app.
void bootloader_preinit(void) {
    // Set PB0 low (RED led).
    GPIO_Init(GPIOB, GPIO_Pin_0, GPIO_Mode_Out_PP_Low_Fast);

    // Set PD2,4,5 low (RGB led).
    GPIO_Init(GPIOD,
            (GPIO_Pin_2 | GPIO_Pin_4 | GPIO_Pin_5),
            GPIO_Mode_Out_PP_Low_Fast);
}

void bootloader_init(void) {
    CLK_SYSCLKDivConfig(CLK_SYSCLKDiv_1);
    CLK_SYSCLKSourceSwitchCmd(ENABLE);
    CLK_SYSCLKSourceConfig(CLK_SYSCLKSource_HSI);
    while (CLK_GetSYSCLKSource() != CLK_SYSCLKSource_HSI);

    // I2C init.
    CLK_PeripheralClockConfig(CLK_Peripheral_I2C1, ENABLE);
    I2C_DeInit(I2C1);
    I2C_Init(I2C1, IOMCU_I2C_NORM_SPEED, (I2C_BTLDR_ADDR << 1),
            I2C_Mode_I2C, I2C_DutyCycle_2, I2C_Ack_Enable,
            I2C_AcknowledgedAddress_7bit);

    I2C_Cmd(I2C1, ENABLE);
}

void bootloader_deinit(void) {
    I2C_DeInit(I2C1);
    CLK_DeInit();
}

void led_hw_init(void) {
}

void led_update(void){
}
