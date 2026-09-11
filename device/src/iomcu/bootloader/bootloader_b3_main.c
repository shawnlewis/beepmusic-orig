#include "stm8l15x.h"

#include "bootloader.h"


// This is used to prevent the LEDs from glitching as much when the
// bootloader is starting.  These do not get reset before starting the app.
void bootloader_preinit(void) {
    // Set LED2 pins all low.
    GPIO_Init(GPIOE,
            (GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5),
            GPIO_Mode_Out_PP_Low_Fast);

    // Set LED COL pins all high
    GPIO_Init(LED_COL_PORT, LED_COL_ALL, LED_COL_GPIO_INIT_MODE);

    // Set LED ROW pins all low.
    GPIO_Init(LED_ROW_PORT, LED_ROW_ALL, LED_ROW_GPIO_INIT_MODE);
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


void led_update(void){
    static uint8_t status_led = 1;
    static uint16_t led_delay = 0;
    uint8_t row;
    uint8_t col;
    
    // brute force a timer for toggling LEDs because there are no interrupts
    if(led_delay < LED_TIMER_VALUE) {
        led_delay++;
    } else {
        //reset timer
        led_delay = 0;
      
        LED_ROW_OFF(LED_ROW_ALL);
        LED_COL_OFF(LED_COL_ALL);
      
        row = (0x01<<(status_led / LED_COL_SIZE));
        col = (0x01<<(status_led % LED_COL_SIZE));
        // Switch LED here
        LED_ROW_ON(row);
        LED_COL_ON(col);
        
        if(++status_led >= 24)
          status_led = 0;
    }

}
