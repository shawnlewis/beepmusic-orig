#include "stm8l15x.h"

#include "bootloader.h"
#include "bootloader_proto.h"
#include "iomcu_checksum.h"


#define BTLDR_EEPROM_DATA_MAGIC                     (0x42454550)
#define BTLDR_EEPROM_DATA_OFFSET                    \
    (FLASH_DATA_EEPROM_START_PHYSICAL_ADDRESS + 0x0)
// Worst case: crc16 of 0xff * 0xe000: ~1.75s.  Actual boot time will be much
// shorter as crc16 is only done on actual app size.
#define BTLDR_EEPROM_DATA_BLOCK                     (0)
#define BTLDR_APP_INFO_SET_TIME                     \
    (I2C_BTLDR_TIME_UNIT_MS | 2500)


typedef struct {
    uint32_t magic;
    uint32_t app_size;
    uint16_t app_crc16;
    uint16_t app_ver;
    uint8_t autoboot;
    uint8_t crc8;  // crc8 of BtldrEEPROMData not including crc8 field.
} BtldrEEPROMData;

uint8_t run_state = RUN_STATE_BTLDR_START;
uint8_t btldr_state = I2C_BTLDR_STATE_OK;
uint8_t app_status;


// This function will use i2c_buf as a scratch space.
void update_app_status(void) {
    BtldrAppInfo app_info;
    // Need to use the correct addressing mode when reading from flash.
    uint8_t PointerAttr *flashp = (PointerAttr uint8_t *)
            ((MemoryAddressCast)(BTLDR_STM8L15X_DATA_BASE));
    uint16_t app_blocks;
    uint16_t crc;
    uint8_t i;

    app_status = I2C_BTLDR_APP_STATUS_READY_RESV;
    read_app_info(&app_info);
    // Default to _NA app status.
    app_status = I2C_BTLDR_APP_STATUS_READY_NA;

    // If info.status is _RESV then eeprom data passed the magic/crc checks.
    if ((app_info.status & I2C_BTLDR_APP_STATUS_READY_MASK) ==
            I2C_BTLDR_APP_STATUS_READY_RESV) {
        // If app size is not within chip flash size limits return _NA
        // since write_app_info shouldn't have allowed this.
        if (app_info.size > BTLDR_STM8L15X_DATA_SIZE) {
            return;
        }

        // Set autoboot flag.
        app_status = (app_info.status & I2C_BTLDR_APP_STATUS_AUTOBOOT_MASK);

        // Check for a valid reset vector.  This is a quick test to see if
        // data has been programmed or not.
        if (*flashp != BTLDR_STM8L15X_INT_OPCODE) {
            app_status |= I2C_BTLDR_APP_STATUS_READY_NO_RESET;
            return;
        }

        // Copy data to i2c_buf so crc16 does not need to use flash
        // addressing mode.
        app_blocks = (uint16_t)(app_info.size / FLASH_BLOCK_SIZE);
        crc = IOMCU_CRC16_INIT;
        while (app_blocks--) {
            for (i = 0; i < FLASH_BLOCK_SIZE; i++) {
                i2c_buf[i] = flashp[i];
            }
            flashp += FLASH_BLOCK_SIZE;
            crc = crc16(crc, i2c_buf, FLASH_BLOCK_SIZE);
        }

        if (crc != app_info.crc16) {
            app_status |= I2C_BTLDR_APP_STATUS_READY_CRC_FAILED;
        } else {
            // All checks passed.
            app_status |= I2C_BTLDR_APP_STATUS_READY_READY;
        }
    }
}

void write_app_info(BtldrAppInfoSet *p) {
    BtldrEEPROMData *data = (BtldrEEPROMData *)i2c_buf;
    uint8_t i;

    for (i = 0; i < FLASH_BLOCK_SIZE; i++) {
        i2c_buf[i] = 0;
    }
    data->magic = BTLDR_EEPROM_DATA_MAGIC;
    data->app_size = p->size;
    data->app_crc16 = p->crc16;
    data->app_ver = p->app_ver;
    data->autoboot = p->autoboot & I2C_BTLDR_APP_STATUS_AUTOBOOT_MASK;
    data->crc8 = crc8(IOMCU_CRC8_INIT, (uint8_t *)data,
            sizeof(BtldrEEPROMData) - sizeof(uint8_t));

    flash_programblock(BTLDR_EEPROM_DATA_BLOCK, FLASH_MemType_Data,
            FLASH_ProgramMode_Standard, i2c_buf);
    while (FLASH_GetFlagStatus(FLASH_FLAG_HVOFF) == RESET);

    update_app_status();
}

void read_app_info(BtldrAppInfo *p) {
    BtldrEEPROMData data;
    uint8_t *bytep = (uint8_t *)&data;
    uint8_t i;
    uint8_t crc;

    for (i = 0; i < sizeof(BtldrEEPROMData); i++) {
        bytep[i] = FLASH_ReadByte(BTLDR_EEPROM_DATA_OFFSET + i);
    }

    crc = crc8(IOMCU_CRC8_INIT, bytep, sizeof(BtldrEEPROMData)
            - sizeof(uint8_t));
    if (crc == data.crc8 && data.magic == BTLDR_EEPROM_DATA_MAGIC) {
        p->size = data.app_size;
        p->crc16 = data.app_crc16;
        p->app_ver = data.app_ver;
        p->status = app_status | data.autoboot;
    } else {
        // Zero out app info.  Status == 0 for NA.
        bytep = (uint8_t *)p;
        for (i = 0; i < sizeof(BtldrAppInfo); i++) {
            bytep[i] = 0;
        }
    }
    p->app_info_set_time = BTLDR_APP_INFO_SET_TIME;
}

void init_run_state(void) {
    update_app_status();

    // If autoboot and safety checks passed start the bootloader
    // start timeout, if not enter the bootloader run state.
    if (app_status == (I2C_BTLDR_APP_STATUS_READY_READY
            | I2C_BTLDR_APP_STATUS_AUTOBOOT_YES)) {
        run_state = RUN_STATE_BTLDR_START;
    } else {
        run_state = RUN_STATE_BTLDR_RUN;
    }
}

void flash_rdwr_run(void) {
    uint8_t i;

    // flash_addr was validated before for the required status read.
    switch (flash_state) {
    case FLASH_STATE_EEPROM_READ_RUN:
    case FLASH_STATE_DATA_READ_RUN:
        // Indicates set flash read out size.
        i2c_out_remaining = FLASH_BLOCK_SIZE;

        for (i = 0; i < FLASH_BLOCK_SIZE; i++) {
            i2c_buf[i] = FLASH_ReadByte(flash_addr + i);
        }
        flash_state = (flash_state == FLASH_STATE_EEPROM_READ_RUN)
                ? FLASH_STATE_EEPROM_READ_DATA_READY
                : FLASH_STATE_DATA_READ_DATA_READY;
        break;

    case FLASH_STATE_EEPROM_WRITE_RUN:
    case FLASH_STATE_DATA_WRITE_RUN: {
        // This part is redundant since the flash routines will just
        // convert the block back into the real address.  It is just
        // to keep the flash interface inline with the stdperiph
        // interface.
        uint32_t start = (flash_state == FLASH_STATE_EEPROM_WRITE_RUN)
                ? FLASH_DATA_EEPROM_START_PHYSICAL_ADDRESS
                : FLASH_PROGRAM_START_PHYSICAL_ADDRESS;
        uint16_t block = (uint16_t)((flash_addr - start) / FLASH_BLOCK_SIZE);
        // The naming convention is opposite from stdperiph, eeprom is data and
        // data is program.
        FLASH_MemType_TypeDef memtype =
                (flash_state == FLASH_STATE_EEPROM_WRITE_RUN)
                ? FLASH_MemType_Data : FLASH_MemType_Program;

        // Part of this function runs out of memory and access to the rest of
        // the bootloader code is halted until writing to flash is done.
        flash_programblock(block, memtype, FLASH_ProgramMode_Standard,
                i2c_buf);

        // Wait until the end of programming.  We shouldn't have to wait
        // here since execution out of flash can't happen until programming
        // is done.
        while (FLASH_GetFlagStatus(FLASH_FLAG_HVOFF) == RESET);

        // Invalidate the app ready flag if we write to data.
        if (flash_state == FLASH_STATE_DATA_WRITE_RUN) {
            app_status &= ~I2C_BTLDR_APP_STATUS_READY_MASK;
            app_status |= I2C_BTLDR_APP_STATUS_READY_NA;
        }

        flash_state = FLASH_STATE_IDLE;
        break;
    }

    default:
        flash_state = FLASH_STATE_IDLE;
        btldr_state = I2C_BTLDR_STATE_FLASH_NOT_READY;
        break;
    }
}

void write_app_info_run(void) {
    // Copy AppInfoSet from i2c_buf to the stack (write_app_info uses
    // i2c_buf as scratch space).
    BtldrAppInfoSet info;
    BtldrAppInfoSet *p = (BtldrAppInfoSet *)i2c_buf;

    info.size = p->size;
    info.crc16 = p->crc16;
    info.app_ver = p->app_ver;
    info.autoboot = p->autoboot;
    write_app_info(&info);

    flash_state = FLASH_STATE_IDLE;
}


// 62.5us was the highest round number timeout achievable with uint16_t.
#ifdef NDEBUG
#define I2C_62500_US_TIMEOUT                    (0x9100)
#else
#define I2C_62500_US_TIMEOUT                    (0xc200)
#endif
// Total 1s timeout.
#define I2C_TIMEOUT_COUNT                       (16)

void main(void) {

    bootloader_preinit();

    // Init the correct run state based on the autoboot flag and if a valid
    // app is detected.
    init_run_state();

    // Init hardware needed for the bootloader.
    bootloader_init();

    uint16_t timeout_counter = I2C_TIMEOUT_COUNT;
    uint16_t timeout = I2C_62500_US_TIMEOUT;
    // Wait for the start key to run the bootloader.
    while (run_state == RUN_STATE_BTLDR_START) {
        timeout = i2c_proc_event(timeout);
        if (!timeout) {
            if (!--timeout_counter) {
                // Timeout done start the app.
                run_state = (run_state == RUN_STATE_BTLDR_RUN)
                        ? run_state : RUN_STATE_APP_START;
            }
            timeout = I2C_62500_US_TIMEOUT;
        }
    }

    if (run_state == RUN_STATE_BTLDR_RUN) {
        // Unlock flash.
        FLASH_SetProgrammingTime(FLASH_ProgramTime_Standard);
        FLASH_Unlock(FLASH_MemType_Program);
        while (FLASH_GetFlagStatus(FLASH_FLAG_PUL) == RESET);
        FLASH_Unlock(FLASH_MemType_Data);
        while (FLASH_GetFlagStatus(FLASH_FLAG_DUL) == RESET);

        do {
            i2c_proc_event(0);
            led_update();

            // This state will be right after an address is received,
            // status read, and if writing to flash the data is also received.
            if (i2c_state == I2C_STATE_IDLE) {
                if ((flash_state == FLASH_STATE_EEPROM_READ_RUN)
                        || (flash_state == FLASH_STATE_DATA_READ_RUN)
                        || (flash_state == FLASH_STATE_EEPROM_WRITE_RUN)
                        || (flash_state == FLASH_STATE_DATA_WRITE_RUN)) {
                    flash_rdwr_run();
                } else if (flash_state == FLASH_STATE_APP_INFO_WRITE_RUN) {
                    write_app_info_run();
                }
            }
        } while (run_state == RUN_STATE_BTLDR_RUN
                || i2c_state != I2C_STATE_IDLE);

        // Lock flash.
        FLASH_Lock(FLASH_MemType_Program);
        FLASH_Lock(FLASH_MemType_Data);
    }

    bootloader_deinit();

    // Jump to the app reset vector.  The IAR compiler will reset the stack
    // pointer at the beginning of the entry point.
    asm("JPF $" APP_BASE_STR);
}

void assert_failed(uint8_t *file, uint32_t line) {
    while (1) {
    }
}
