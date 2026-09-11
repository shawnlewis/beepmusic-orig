#ifndef __STM8L15x_CONF_H
#define __STM8L15x_CONF_H

#include "stm8l15x.h"

// Uncomment the feature include as needed (this file is inlcuded from stdperiph).
#include "stm8l15x_adc.h"
//#include "stm8l15x_aes.h"
//#include "stm8l15x_beep.h"
#include "stm8l15x_clk.h"
//#include "stm8l15x_comp.h"
//#include "stm8l15x_dac.h"
#include "stm8l15x_dma.h"
//#include "stm8l15x_exti.h"
//#include "stm8l15x_flash.h"
#include "stm8l15x_gpio.h"
#include "stm8l15x_i2c.h"
//#include "stm8l15x_irtim.h"
#include "stm8l15x_itc.h"
//#include "stm8l15x_iwdg.h"
//#include "stm8l15x_lcd.h"
//#include "stm8l15x_pwr.h"
//#include "stm8l15x_rst.h"
//#include "stm8l15x_rtc.h"
//#include "stm8l15x_spi.h"
#include "stm8l15x_syscfg.h"
//#include "stm8l15x_tim1.h"
#include "stm8l15x_tim2.h"
#include "stm8l15x_tim3.h"
#include "stm8l15x_tim4.h"
//#include "stm8l15x_tim5.h"
//#include "stm8l15x_usart.h"
//#include "stm8l15x_wfe.h"
//#include "stm8l15x_wwdg.h"

//#define USE_FULL_ASSERT
#ifdef  USE_FULL_ASSERT
#define assert_param(expr) ((expr) ? (void)0 : assert_failed((uint8_t *)__FILE__, __LINE__))
void assert_failed(uint8_t *file, uint32_t line);
#else
#define assert_param(expr) ((void)0)
#endif

#endif  // __STM8L15x_CONF_H
