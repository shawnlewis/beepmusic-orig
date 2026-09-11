#ifndef IOMCU_BOOTLOADER_H
#define IOMCU_BOOTLOADER_H

// Keep these values inline with the app linker scripts.
#define APP_BASE a000
#define APP_BASE_DC24 0xa000

#ifndef __IAR_SYSTEMS_ASM__

#define _STR(X) __STR(X)
#define __STR(X) #X
#define APP_BASE_STR _STR(APP_BASE)


#include "stm8l15x.h"
#include "bootloader_proto.h"

#define BTLDR_VERSION                           (5)


#define BTLDR_STM8L15X_INT_OPCODE               (0x82)
#define BTLDR_STM8L15X_EEPROM_BASE              \
    (FLASH_DATA_EEPROM_START_PHYSICAL_ADDRESS \
    + (IOMCU_BTLDR_RESV_EEPROM_BLOCKS * FLASH_BLOCK_SIZE))
#define BTLDR_STM8L15X_EEPROM_SIZE              \
    ((FLASH_DATA_EEPROM_BLOCKS_NUMBER - IOMCU_BTLDR_RESV_EEPROM_BLOCKS) \
    * FLASH_BLOCK_SIZE)
#define BTLDR_STM8L15X_DATA_BASE                \
    (FLASH_PROGRAM_START_PHYSICAL_ADDRESS \
    + (IOMCU_BTLDR_RESV_DATA_BLOCKS * FLASH_BLOCK_SIZE))
#define BTLDR_STM8L15X_DATA_SIZE                \
    ((FLASH_PROGRAM_BLOCKS_NUMBER - IOMCU_BTLDR_RESV_DATA_BLOCKS) \
    * FLASH_BLOCK_SIZE)
// Only if FLASH_BLOCK_SIZE == 0x80.
#define BTLDR_STM8L15X_INVALID_ADDR_MASK        (0x7f)


#define RUN_STATE_BTLDR_START                   (0)
#define RUN_STATE_BTLDR_RUN                     (1)
#define RUN_STATE_APP_START                     (2)

extern uint8_t run_state;
extern uint8_t btldr_state;
extern uint8_t app_status;

// Board customization functions.
void bootloader_preinit(void);
void bootloader_init(void);
void bootloader_deinit(void);

void read_app_info(BtldrAppInfo *p);

// General GPIO defines.
#define GPIO_HIGH(PORT, PINS)           (PORT)->ODR |= (PINS)
#define GPIO_LOW(PORT, PINS)            (PORT)->ODR &= ~(PINS)
#define GPIO_TOGGLE(PORT, PINS)         (PORT)->ODR ^= (PINS)
#define GPIO_STATE(PORT, PINS)          ((PORT)->IDR & (PINS))
#define GPIO_STATE_HIGH(PORT, PINS)     ((((PORT)->IDR & (PINS)) == (PINS)) ? 1 : 0)
#define GPIO_STATE_LOW(PORT, PINS)      ((!((PORT)->IDR & (PINS))) ? 1 : 0)

// LED array defines.
#define LED_TIMER_VALUE 0x300

#ifndef LED_ROW_COL_SWAP

#define LED_ROW_SIZE                    (4)
#define LED_ROW_PORT                    (GPIOD)
#define LED_ROW_0                       (GPIO_Pin_0)
#define LED_ROW_1                       (GPIO_Pin_1)
#define LED_ROW_2                       (GPIO_Pin_2)
#define LED_ROW_3                       (GPIO_Pin_3)
#define LED_ROW_ALL \
    (LED_ROW_0 | LED_ROW_1 | LED_ROW_2 | LED_ROW_3)
#define LED_COL_SIZE                    (6)
#define LED_COL_PORT                    (GPIOB)
#define LED_COL_0                       (GPIO_Pin_0)
#define LED_COL_1                       (GPIO_Pin_1)
#define LED_COL_2                       (GPIO_Pin_2)
#define LED_COL_3                       (GPIO_Pin_3)
#define LED_COL_4                       (GPIO_Pin_4)
#define LED_COL_5                       (GPIO_Pin_5)
#define LED_COL_ALL \
    (LED_COL_0 | LED_COL_1 | LED_COL_2 | LED_COL_3 | LED_COL_4 | LED_COL_5)

#define LED_ROW_ON(ROWS)                GPIO_HIGH(LED_ROW_PORT, (ROWS))
#define LED_ROW_OFF(ROWS)               GPIO_LOW(LED_ROW_PORT, (ROWS))
#define LED_COL_ON(COLS)                GPIO_LOW(LED_COL_PORT, (COLS))
#define LED_COL_OFF(COLS)               GPIO_HIGH(LED_COL_PORT, (COLS))
#define LED_ROW_GPIO_INIT_MODE          (GPIO_Mode_Out_PP_Low_Fast)
#define LED_COL_GPIO_INIT_MODE          (GPIO_Mode_Out_PP_High_Fast)

#else  // LED_ROW_COL_SWAP

#define LED_ROW_SIZE                    (6)
#define LED_ROW_PORT                    (GPIOB)
#define LED_ROW_0                       (GPIO_Pin_0)
#define LED_ROW_1                       (GPIO_Pin_1)
#define LED_ROW_2                       (GPIO_Pin_2)
#define LED_ROW_3                       (GPIO_Pin_3)
#define LED_ROW_4                       (GPIO_Pin_4)
#define LED_ROW_5                       (GPIO_Pin_5)
#define LED_ROW_ALL \
    (LED_ROW_0 | LED_ROW_1 | LED_ROW_2 | LED_ROW_3 | LED_ROW_4 | LED_ROW_5)
#define LED_COL_SIZE                    (4)
#define LED_COL_PORT                    (GPIOD)
#define LED_COL_0                       (GPIO_Pin_0)
#define LED_COL_1                       (GPIO_Pin_1)
#define LED_COL_2                       (GPIO_Pin_2)
#define LED_COL_3                       (GPIO_Pin_3)
#define LED_COL_ALL \
    (LED_COL_0 | LED_COL_1 | LED_COL_2 | LED_COL_3)

#define LED_ROW_ON(ROWS)                GPIO_LOW(LED_ROW_PORT, (ROWS))
#define LED_ROW_OFF(ROWS)               GPIO_HIGH(LED_ROW_PORT, (ROWS))
#define LED_COL_ON(COLS)                GPIO_HIGH(LED_COL_PORT, (COLS))
#define LED_COL_OFF(COLS)               GPIO_LOW(LED_COL_PORT, (COLS))
#define LED_ROW_GPIO_INIT_MODE          (GPIO_Mode_Out_PP_High_Fast)
#define LED_COL_GPIO_INIT_MODE          (GPIO_Mode_Out_PP_Low_Fast)

#endif  // LED_ROW_COL_SWAP

#define LED_ROW_TOGGLE(ROWS)            GPIO_TOGGLE(LED_ROW_PORT, (ROWS))
#define LED_COL_TOGGLE(COLS)            GPIO_TOGGLE(LED_COL_PORT, (ROWS))

#define LED_TOT_SIZE                    (LED_ROW_SIZE * LED_COL_SIZE)
#define LED_OP_COUNT                    (LED_ROW_SIZE * (LED_COL_SIZE + 1))
#define LED_OP_COL_MASK                 (LED_COL_ALL)
#define LED_OP_ROW_INC                  (1<<6)
#define LED_OP_LOOP                     (1<<7)

//extern uint16_t led_delay = 0;

void led_update(void);


#define I2C_BUF_SIZE                            (192)

#define I2C_STATE_IDLE                          (0)
#define I2C_STATE_REG_WAIT                      (1)
#define I2C_STATE_DATA_IN                       (2)
#define I2C_STATE_OUT_START_WAIT                (3)
#define I2C_STATE_DATA_OUT                      (4)

extern uint8_t i2c_state;
// The stm8 has an alignment requirement of 1 for all data types.  Reading
// writing any data type to any address within i2c_buf is safe.  The buffer
// has two additional bytes for checksum and trailing readout byte.
extern uint8_t i2c_buf[I2C_BUF_SIZE + 2];
extern uint8_t i2c_offset;
extern uint8_t i2c_out_remaining;

uint16_t i2c_proc_event(uint16_t timeout);


// States are defined as:
// *_ADDR_READY: Address has been written and is valid.
// *_WRITE_DATA_READY: The correct number of bytes has been written into
//   the i2c buffer and passed the checksum.
// *_RUN: Main should process the rd/wr operation when I2C next goes idle.
// *_READ_DATA_READY: The i2c buffer contains data to be read out.
#define FLASH_STATE_IDLE                        (0)
#define FLASH_STATE_EEPROM_READ_ADDR_READY      (1)
#define FLASH_STATE_EEPROM_READ_RUN             (2)
#define FLASH_STATE_EEPROM_READ_DATA_READY      (3)
#define FLASH_STATE_EEPROM_WRITE_ADDR_READY     (4)
#define FLASH_STATE_EEPROM_WRITE_DATA_READY     (5)
#define FLASH_STATE_EEPROM_WRITE_RUN            (6)
#define FLASH_STATE_DATA_READ_ADDR_READY        (7)
#define FLASH_STATE_DATA_READ_RUN               (8)
#define FLASH_STATE_DATA_READ_DATA_READY        (9)
#define FLASH_STATE_DATA_WRITE_ADDR_READY       (10)
#define FLASH_STATE_DATA_WRITE_DATA_READY       (11)
#define FLASH_STATE_DATA_WRITE_RUN              (12)
#define FLASH_STATE_APP_INFO_WRITE_RUN          (13)


extern uint8_t flash_state;
extern uint32_t flash_addr;

// Use two functions to prevent any ambiguity based on the rw intent of the
// i2c master.
void cmd_data_out(uint8_t cmd);
void cmd_data_in(uint8_t cmd);


// These are close replacements for the stdperiph functions.
void flash_programblock(uint16_t block, FLASH_MemType_TypeDef memtype,
        FLASH_ProgramMode_TypeDef prog_mode, uint8_t *buf);
void flash_eraseblock(uint16_t block, FLASH_MemType_TypeDef memtype);


#endif  // __IAR_SYSTEMS_ASM__

#endif  // IOMCU_BOOTLOADER_H
