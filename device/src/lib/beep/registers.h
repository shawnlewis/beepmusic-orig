#ifndef BEEP_REGISTERS_H
#define BEEP_REGISTERS_H

#include <stdint.h>

#define PAGE_SIZE 4096

#define NUM_GPIOS

#define GPIO_BASE_ADDRESS 0x18040000

#define GPIO_OE           0x00
#define GPIO_IN           0x04
#define GPIO_OUT          0x08
#define GPIO_SET          0x0C
#define GPIO_CLEAR        0x10
#define GPIO_FUNCTION_1   0x28
#define GPIO_FUNCTION_2   0x30

#define JUMPSTART_DISABLE_BIT (1 << 9)
#define WPS_DISABLE_BIT (1 << 8)

#define ETH_SWITCH_LED_ENABLES \
    ((1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7))


static const int INPUT_OUTPUTS[] = {6, 7, 8, 11, 12, 26, 27, 28};
#define NUM_INPUT_OUTPUTS (sizeof(INPUT_OUTPUTS) / sizeof(int))
static const int OUTPUTS[] = {13, 14, 15, 16, 17};
#define NUM_OUTPUTS (sizeof(OUTPUTS) / sizeof(int))

void* map_page(uint32_t address);
uint32_t read_word(void* page, uint32_t offset);
void write_word(void* page, uint32_t offset, uint32_t val);
void clear_bits(void* page, uint32_t offset, uint32_t mask);
void set_bits(void* page, uint32_t offset, uint32_t mask);
void print_word(uint32_t word);
void print_word_as_bits(uint32_t word);
void print_bit_labels(void);

// configures all registers needed to turn on all gpios
// returns the configuration page needed for other functions
void* init(void);

bool set_output_enable(void* page, int gpio, bool enable);

#endif  // BEEP_REGISTERS_H
