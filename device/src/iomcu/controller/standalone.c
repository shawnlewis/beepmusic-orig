#include "stm8l15x.h"

#include "controller.h"

uint8_t quadrant = 0;

void update_standalone_leds(void)
{
  uint8_t i;
  
  if(quadrant > 3)
    quadrant = 0;
  
  for(i=0; i<24; i++)
    led_pwm_table[i] = 0;
  
  for (i = quadrant * 6; i < (quadrant+1)*6; i++)
    led_pwm_table[i] = 255;
  
  led_state |= LED_STATE_PWM_READY;
  
}

