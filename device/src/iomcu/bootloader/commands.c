#include "stm8l15x.h"

#include "bootloader.h"
#include "bootloader_proto.h"


// stm8l datasheet 7.
#define BTLDR_STM8L15X_OPTION_BYTE_0            (0x4800)
#define BTLDR_STM8L15X_OPTION_BYTE_1            (0x4802)
#define BTLDR_STM8L15X_OPTION_BYTE_2            (0x4807)
#define BTLDR_STM8L15X_OPTION_BYTE_3            (0x4808)
#define BTLDR_STM8L15X_OPTION_BYTE_4            (0x4809)
#define BTLDR_STM8L15X_OPTION_BYTE_5            (0x480A)
#define BTLDR_STM8L15X_OPTION_BYTE_6            (0x480B)
#define BTLDR_STM8L15X_OPTION_BYTE_7            (0x480C)
// stm8l datasheet 8.
#define BTLDR_STM8L15X_UNIQUE_ID_BASE           (0x4926)
// stm8l datasheet 9.3.5.
#define BTLDR_STM8L15X_FLASH_WRITE_TIME         (I2C_BTLDR_TIME_UNIT_MS | 8)
#define BTLDR_STM8L15X_FLASH_READ_TIME          (I2C_BTLDR_TIME_UNIT_US | 10)

uint8_t flash_state = FLASH_STATE_IDLE;
uint32_t flash_addr;


void cmd_data_out(uint8_t cmd) {
    // I2C state is not reset after a read command since the transaction is
    // not finished.  Appropriate values for i2c_buf, i2c_offset and
    // i2c_out_remaining are set by the command (or the main loop reading
    // flash).

    // Set default error case (may be changed by I2C_BTLDR_REG_*_READ).
    uint8_t error_btldr_state = I2C_BTLDR_STATE_INVALID_REG;

    // Store current bootloader and flash state.
    uint8_t last_btldr_state = btldr_state;
    uint8_t last_flash_state = flash_state;

    // Default case for most operations.
    btldr_state = I2C_BTLDR_STATE_OK;
    i2c_offset = 0;
    flash_state = FLASH_STATE_IDLE;

    switch (cmd) {
    case I2C_BTLDR_REG_BTLDR_INFO: {
        BtldrInfo *p = (BtldrInfo *)i2c_buf;
        p->endian = I2C_BTLDR_INFO_SLAVE_ENDIAN;
        p->proto_ver = I2C_BTLDR_INFO_PROTO_VERSION;
        p->btldr_ver = BTLDR_VERSION;
        i2c_out_remaining = I2C_BTLDR_REG_BTLDR_INFO_SIZE;
        break;
    }

    case I2C_BTLDR_REG_STATUS:
        // Reading the status is used to initiate flash read operations and
        // set the write ready state.
        if (last_flash_state != FLASH_STATE_IDLE
                && last_btldr_state == I2C_BTLDR_STATE_OK) {
            switch (last_flash_state) {
            case FLASH_STATE_EEPROM_READ_ADDR_READY:
                flash_state = FLASH_STATE_EEPROM_READ_RUN;
                break;

            case FLASH_STATE_DATA_READ_ADDR_READY:
                flash_state = FLASH_STATE_DATA_READ_RUN;
                break;

            case FLASH_STATE_EEPROM_WRITE_ADDR_READY:
                flash_state = FLASH_STATE_EEPROM_WRITE_DATA_READY;
                break;

            case FLASH_STATE_DATA_WRITE_ADDR_READY:
                flash_state = FLASH_STATE_DATA_WRITE_DATA_READY;
                break;

            default:
                // Flash state is already set to _IDLE.
                // last_btldr_state will be sent back to the master. This
                // status would have to be read out with another status read.
                btldr_state = I2C_BTLDR_STATE_FLASH_INTERRUPTED;
                break;
            }
        }

        i2c_buf[0] = last_btldr_state;
        i2c_out_remaining = I2C_BTLDR_REG_STATUS_SIZE;
        break;

    case I2C_BTLDR_REG_DEV_INFO: {
        uint8_t i;
        BtldrDeviceInfo *p = (BtldrDeviceInfo *)i2c_buf;
        p->board_id = BTLDR_DEV_INFO_BRD_ID;
        p->board_rev = BTLDR_DEV_INFO_BRD_REV;
        p->chip_id = BTLDR_DEV_INFO_CHIP_ID;
        for (i = 0; i < 12; i++) {
            p->chip_serial[i] = FLASH_ReadByte(
                    BTLDR_STM8L15X_UNIQUE_ID_BASE + i);
        }
        // The option bytes are not sequential.
        p->option_bytes[0] = FLASH_ReadByte(BTLDR_STM8L15X_OPTION_BYTE_0);
        p->option_bytes[1] = FLASH_ReadByte(BTLDR_STM8L15X_OPTION_BYTE_1);
        p->option_bytes[2] = FLASH_ReadByte(BTLDR_STM8L15X_OPTION_BYTE_2);
        p->option_bytes[3] = FLASH_ReadByte(BTLDR_STM8L15X_OPTION_BYTE_3);
        p->option_bytes[4] = FLASH_ReadByte(BTLDR_STM8L15X_OPTION_BYTE_4);
        p->option_bytes[5] = FLASH_ReadByte(BTLDR_STM8L15X_OPTION_BYTE_5);
        p->option_bytes[6] = FLASH_ReadByte(BTLDR_STM8L15X_OPTION_BYTE_6);
        p->option_bytes[7] = FLASH_ReadByte(BTLDR_STM8L15X_OPTION_BYTE_7);
        i2c_out_remaining = I2C_BTLDR_REG_DEV_INFO_SIZE;
        break;
    }

    case I2C_BTLDR_REG_FLASH_INFO: {
        BtldrFlashInfo *p = (BtldrFlashInfo *)i2c_buf;
        p->eeprom_base = BTLDR_STM8L15X_EEPROM_BASE;
        p->eeprom_size = BTLDR_STM8L15X_EEPROM_SIZE;
        p->data_base = BTLDR_STM8L15X_DATA_BASE;
        p->data_size = BTLDR_STM8L15X_DATA_SIZE;
        p->eeprom_blocksize = FLASH_BLOCK_SIZE;
        p->data_blocksize = FLASH_BLOCK_SIZE;
        p->eeprom_read_time = BTLDR_STM8L15X_FLASH_READ_TIME;
        p->eeprom_write_time = BTLDR_STM8L15X_FLASH_WRITE_TIME;
        p->data_read_time = BTLDR_STM8L15X_FLASH_READ_TIME;
        p->data_write_time = BTLDR_STM8L15X_FLASH_WRITE_TIME;
        i2c_out_remaining = I2C_BTLDR_REG_FLASH_INFO_SIZE;
        break;
    }

    case I2C_BTLDR_REG_APP_INFO: {
        BtldrAppInfo *p = (BtldrAppInfo *)i2c_buf;
        read_app_info(p);
        i2c_out_remaining = I2C_BTLDR_REG_APP_INFO_SIZE;
        break;
    }

    case I2C_BTLDR_REG_EEPROM_READ:
    case I2C_BTLDR_REG_DATA_READ: {
        // Check that ready data is the correct size and the flash state
        // is correct for either EEPROM or data.
        if ((i2c_out_remaining == FLASH_BLOCK_SIZE)
                && ((cmd == I2C_BTLDR_REG_EEPROM_READ)
                && (last_flash_state == FLASH_STATE_EEPROM_READ_DATA_READY)
                || ((cmd == I2C_BTLDR_REG_DATA_READ)
                && (last_flash_state == FLASH_STATE_DATA_READ_DATA_READY)))) {
            // i2c_offset is already set above.
            flash_state = FLASH_STATE_IDLE;
            break;
        }
        // Set the correct error state and fall down to the default
        // to fill data out with 0xff.
        error_btldr_state = I2C_BTLDR_STATE_DATA_NOT_READY;
    }

    default: {
        // Give the master a chance to read out data to not trigger
        // the underflow error so it can read the invalid cmd error.
        for (i2c_offset = 0; i2c_offset < I2C_BUF_SIZE; i2c_offset++) {
            i2c_buf[i2c_offset] = 0xff;
        }
        i2c_offset = 0;  // Need to reset.
        i2c_out_remaining = I2C_BUF_SIZE;
        // Default _INVALID_REG or _DATA_NOT_READY by I2C_BTLDR_REG_*_READ.
        btldr_state = error_btldr_state;
        break;
    }
    }
}

static inline void set_addr_ready_flash_state(uint8_t cmd) {
    switch (cmd) {
    case I2C_BTLDR_REG_EEPROM_READ_ADDR:
        flash_state = FLASH_STATE_EEPROM_READ_ADDR_READY;
        break;

    case I2C_BTLDR_REG_DATA_READ_ADDR:
        flash_state = FLASH_STATE_DATA_READ_ADDR_READY;
        break;

    case I2C_BTLDR_REG_EEPROM_WRITE_ADDR:
        flash_state = FLASH_STATE_EEPROM_WRITE_ADDR_READY;
        break;

    case I2C_BTLDR_REG_DATA_WRITE_ADDR:
        flash_state = FLASH_STATE_DATA_WRITE_ADDR_READY;
        break;

    default:
        flash_state = FLASH_STATE_IDLE;
        break;
    }
}

static inline uint8_t check_flash_addr(uint8_t cmd, uint32_t addr) {
    if (addr & BTLDR_STM8L15X_INVALID_ADDR_MASK) {
        return I2C_BTLDR_STATE_INVALID_ADDR;
    }

    switch (cmd) {
    case I2C_BTLDR_REG_EEPROM_READ_ADDR:
    case I2C_BTLDR_REG_EEPROM_WRITE_ADDR:
        if ((addr < BTLDR_STM8L15X_EEPROM_BASE)
                || (addr >= (BTLDR_STM8L15X_EEPROM_BASE
                + BTLDR_STM8L15X_EEPROM_SIZE))) {
            return I2C_BTLDR_STATE_INVALID_ADDR;
        }
        break;

    case I2C_BTLDR_REG_DATA_READ_ADDR:
    case I2C_BTLDR_REG_DATA_WRITE_ADDR:
        if ((addr < BTLDR_STM8L15X_DATA_BASE)
                || (addr >= (BTLDR_STM8L15X_DATA_BASE
                + BTLDR_STM8L15X_DATA_SIZE))) {
            return I2C_BTLDR_STATE_INVALID_ADDR;
        }
        break;

    default:
        break;
    }

    return I2C_BTLDR_STATE_OK;
}

void cmd_data_in(uint8_t cmd) {
    // I2C state is reset by caller after a write command is completed.

    // Store flash state.
    uint8_t last_flash_state = flash_state;

    // Default case for most operations.
    btldr_state = I2C_BTLDR_STATE_OK;
    flash_state = FLASH_STATE_IDLE;

    switch (cmd) {
    case I2C_BTLDR_REG_START:
        if (i2c_offset == I2C_BTLDR_REG_START_SIZE) {
            BtldrStart *p = (BtldrStart *)i2c_buf;
            // Keys are always sent MSB (KEY_3) first.
            if ((p->key[0] == I2C_BTLDR_START_KEY_3)
                    && (p->key[1] == I2C_BTLDR_START_KEY_2)
                    && (p->key[2] == I2C_BTLDR_START_KEY_1)
                    && (p->key[3] == I2C_BTLDR_START_KEY_0)) {
                switch (run_state) {
                case RUN_STATE_BTLDR_START:
                    run_state = RUN_STATE_BTLDR_RUN;
                    break;

                case RUN_STATE_BTLDR_RUN: {
                    if ((app_status & I2C_BTLDR_APP_STATUS_READY_MASK)
                            == I2C_BTLDR_APP_STATUS_READY_READY) {
                        run_state = RUN_STATE_APP_START;
                    }else {
                        btldr_state = I2C_BTLDR_STATE_APP_NOT_READY;
                    }
                    break;
                }

                default:
                    btldr_state = I2C_BTLDR_STATE_BUSY;
                    break;
                }
            } else {
                btldr_state = I2C_BTLDR_STATE_BAD_KEY;
            }
        } else {
            btldr_state = I2C_BTLDR_STATE_INVALID_I2C_SIZE;
        }
        break;
    
    case I2C_BTLDR_REG_WAIT:
        if (i2c_offset == I2C_BTLDR_REG_START_SIZE) {
            BtldrStart *p = (BtldrStart *)i2c_buf;
            // Keys are always sent MSB (KEY_3) first.
            if ((p->key[0] == I2C_BTLDR_START_KEY_3)
                    && (p->key[1] == I2C_BTLDR_START_KEY_2)
                    && (p->key[2] == I2C_BTLDR_START_KEY_1)
                    && (p->key[3] == I2C_BTLDR_START_KEY_0)) {
                switch (run_state) {
                case RUN_STATE_BTLDR_START:
                    run_state = RUN_STATE_BTLDR_RUN;
                    break;

                case RUN_STATE_BTLDR_RUN:
                    run_state = RUN_STATE_BTLDR_RUN;
                    break;
                
                default:
                    btldr_state = I2C_BTLDR_STATE_BUSY;
                    break;
                }
            } else {
                btldr_state = I2C_BTLDR_STATE_BAD_KEY;
            }
        } else {
            btldr_state = I2C_BTLDR_STATE_INVALID_I2C_SIZE;
        }
        break;

    case I2C_BTLDR_REG_APP_INFO_SET:
        if (i2c_offset == I2C_BTLDR_REG_APP_INFO_SET_SIZE) {
            flash_state = FLASH_STATE_APP_INFO_WRITE_RUN;
        } else {
            btldr_state = I2C_BTLDR_STATE_INVALID_I2C_SIZE;
        }
        break;

    // Writing to the address registers will always reset flash state.
    case I2C_BTLDR_REG_EEPROM_READ_ADDR:
    case I2C_BTLDR_REG_DATA_READ_ADDR:
    case I2C_BTLDR_REG_EEPROM_WRITE_ADDR:
    case I2C_BTLDR_REG_DATA_WRITE_ADDR: {
        BtldrFlashRWAddr *p = (BtldrFlashRWAddr *)i2c_buf;
        btldr_state = check_flash_addr(cmd, p->addr);
        // If address was invalid flash_state is already FLASH_STATE_IDLE.
        if (btldr_state == I2C_BTLDR_STATE_OK) {
            flash_addr = p->addr;
            set_addr_ready_flash_state(cmd);
        }
        break;
    }

    case I2C_BTLDR_REG_EEPROM_WRITE:
    case I2C_BTLDR_REG_DATA_WRITE:
        if (i2c_offset == FLASH_BLOCK_SIZE) {
            if (last_flash_state == FLASH_STATE_EEPROM_WRITE_DATA_READY
                    && cmd == I2C_BTLDR_REG_EEPROM_WRITE) {
                flash_state = FLASH_STATE_EEPROM_WRITE_RUN;
            } else if (last_flash_state == FLASH_STATE_DATA_WRITE_DATA_READY
                    && cmd == I2C_BTLDR_REG_DATA_WRITE) {
                flash_state = FLASH_STATE_DATA_WRITE_RUN;
            } else {
                flash_state = FLASH_STATE_IDLE;
                btldr_state = I2C_BTLDR_STATE_FLASH_NOT_READY;
            }
            break;
        } else {
            btldr_state = I2C_BTLDR_STATE_INVALID_BLOCK_SIZE;
        }
        break;

    default:
        btldr_state = I2C_BTLDR_STATE_INVALID_REG;
        break;
    }
}
