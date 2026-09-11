#include "stm8l15x.h"

#include "controller.h"

uint8_t startup_mode = 0;

uint8_t led_startup_lut[LED_TOT_SIZE] = {
      0,   0,   0, 255,   0,   0,
      0, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255,   0,
      0,   0, 255,   0,   0,   0,
};

uint8_t led_startup_blink_ontime[LED_BLINK_SEQUENCE_NUM] = {
      16, 8, 24, 1, 24
};

uint8_t led_startup_blink_offtime[LED_BLINK_SEQUENCE_NUM] = {
      125, 250, 65, 65, 125 
};

void enter_startup_mode(void)
{
    startup_mode |= STARTUP_MODE_ENABLE;
    
}

void exit_startup_mode(void)
{
  TIM3_ITConfig(TIM3_IT_Update, DISABLE);
  TIM3_Cmd(DISABLE);
}

void draw_startup_pattern(void)
{
  uint8_t i;
  
  for(i=0; i<LED_TOT_SIZE; i++)
  {
    led_pwm_table[i] = led_startup_lut[i];
  }
  
  led_state |= LED_STATE_PWM_READY;
}
