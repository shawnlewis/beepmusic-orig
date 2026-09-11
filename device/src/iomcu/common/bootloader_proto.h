#ifndef IOMCU_BOOTLOADER_PROTO_H
#define IOMCU_BOOTLOADER_PROTO_H


#include "iomcu_shared.h"

// Reading and writing to flash may not be fast enough to prevent clock
// stretching on the i2c bus.  While writing flash the processor may be
// unavailable to service i2c requests as well causing bus errors.  To
// prevent errors and provide fast feedback to the master reading and writing
// to flash is a multistep process.

// Reading from flash:
// 1) Write address to I2C_BTLDR_REG_*_READ_ADDR.
// 2) Read I2C_BTLDR_REG_STATUS, verify STATE_OK.
// 3) Slave will start reading from flash.
// 4) Delay appropriate read time (no other traffic to slave allowed).
// 5) Read block size bytes from I2C_BTLDR_REG_*_READ.

// Writing to flash:
// 1) Write address to I2C_BTLDR_REG_*_WRITE_ADDR.
// 2) Read I2C_BTLDR_REG_STATUS, verify STATE_OK.
// 3) Write block size bytes to I2C_BTLDR_REG_EEPROM_WRITE.
// 4) Slave will start writing to flash.
// 5) Delay appropriate write time (no other traffic to slave allowed).
// 6) Read I2C_BTLDR_REG_STATUS, verify STATE_OK.

// Read and write must be done on block boundaries and as whole blocks.

// Block sizes and delay times are found from reading I2C_BTLDR_REG_FLASH_INFO.

// Writing to data flash will invalidate the APP_READY flag.  For performance
// reasons this flag will not be updated until a write to
// I2C_BTLDR_REG_APP_INFO_SET.  This register has a required delay time found
// from reading I2C_BTLDR_REG_APP_INFO.  The app size is used for crc
// verification before booting must also be whole blocks.

// Delay times are 16 bit values.  The numerical value is stored in the lower
// 7 bits.  If bit 8 is set to 0 the unit is microseconds, 1 is for
// milliseconds.


#define I2C_BTLDR_ADDR                              (0x22)

// Read write as seen by the master.
#define I2C_BTLDR_REG_RW_MASK                       (0x80)
#define I2C_BTLDR_REG_READ                          (0x00)
#define I2C_BTLDR_REG_WRITE                         (0x80)

#define I2C_BTLDR_REG_BTLDR_INFO                    (0x00)
#define I2C_BTLDR_REG_STATUS                        (0x01)
#define I2C_BTLDR_REG_DEV_INFO                      (0x02)
#define I2C_BTLDR_REG_FLASH_INFO                    (0x03)
#define I2C_BTLDR_REG_APP_INFO                      (0x04)
#define I2C_BTLDR_REG_EEPROM_READ                   (0x05)
#define I2C_BTLDR_REG_DATA_READ                     (0x06)
#define I2C_BTLDR_REG_RESV_2                        (0x7f)
#define I2C_BTLDR_REG_START                         (0x80)
#define I2C_BTLDR_REG_WAIT                          (0x82)
#define I2C_BTLDR_REG_APP_INFO_SET                  (0x84)
#define I2C_BTLDR_REG_EEPROM_READ_ADDR              (0x85)
#define I2C_BTLDR_REG_DATA_READ_ADDR                (0x86)
#define I2C_BTLDR_REG_EEPROM_WRITE_ADDR             (0x87)
#define I2C_BTLDR_REG_EEPROM_WRITE                  (0x88)
#define I2C_BTLDR_REG_DATA_WRITE_ADDR               (0x89)
#define I2C_BTLDR_REG_DATA_WRITE                    (0x8a)
#define I2C_BTLDR_REG_RESV                          (0xff)

#define I2C_BTLDR_REG_VAR_SIZE                      (0xff)
#define I2C_BTLDR_REG_BTLDR_INFO_SIZE               (8)
#define I2C_BTLDR_REG_STATUS_SIZE                   (1)
#define I2C_BTLDR_REG_DEV_INFO_SIZE_V1              (18)
#define I2C_BTLDR_REG_DEV_INFO_SIZE                 (26)
#define I2C_BTLDR_REG_FLASH_INFO_SIZE               (28)
#define I2C_BTLDR_REG_APP_INFO_SIZE                 (11)
#define I2C_BTLDR_REG_EEPROM_READ_SIZE              (0xff)
#define I2C_BTLDR_REG_DATA_READ_SIZE                (0xff)
#define I2C_BTLDR_REG_START_SIZE                    (IOMCU_KEY_SIZE)
#define I2C_BTLDR_REG_APP_INFO_SET_SIZE             (9)
#define I2C_BTLDR_REG_FLASH_RW_ADDR_SIZE            (4)
#define I2C_BTLDR_REG_EEPROM_READ_ADDR_SIZE         (I2C_BTLDR_REG_FLASH_RW_ADDR_SIZE)
#define I2C_BTLDR_REG_DATA_READ_ADDR_SIZE           (I2C_BTLDR_REG_FLASH_RW_ADDR_SIZE)
#define I2C_BTLDR_REG_EEPROM_WRITE_ADDR_SIZE        (I2C_BTLDR_REG_FLASH_RW_ADDR_SIZE)
#define I2C_BTLDR_REG_EEPROM_WRITE_SIZE             (0xff)
#define I2C_BTLDR_REG_DATA_WRITE_ADDR_SIZE          (I2C_BTLDR_REG_FLASH_RW_ADDR_SIZE)
#define I2C_BTLDR_REG_DATA_WRITE_SIZE               (0xff)
#define I2C_BTLDR_REG_RESV_SIZE                     (0)


#define I2C_BTLDR_INFO_SLAVE_ENDIAN                 (0x87654321)
#define I2C_BTLDR_INFO_PROTO_VERSION                (2)

#define I2C_DEV_INFO_BRD_ID_NA                      (0x0000)
#define I2C_DEV_INFO_BRD_ID_B2_ENC                  (0x0001)
#define I2C_DEV_INFO_BRD_ID_B3_MAIN                 (0x0002)
#define I2C_DEV_INFO_BRD_ID_DEMO_BOARD_1            (0x0003)

#define I2C_DEV_INFO_CHIP_ID_NA                     (0x0000)
#define I2C_DEV_INFO_CHIP_ID_STM8L152C6             (0x0001)
#define I2C_DEV_INFO_CHIP_ID_STM8L152C8             (0x0002)
#define I2C_DEV_INFO_CHIP_ID_STM8L151G6             (0x0003)

#define I2C_BTLDR_TIME_UNIT_MASK                    (0x8000)
#define I2C_BTLDR_TIME_UNIT_US                      (0x0000)
#define I2C_BTLDR_TIME_UNIT_MS                      (0x8000)
#define I2C_BTLDR_TIME_NUM_MASK                     (0x7fff)

// APP_STATUS_NA indicates bootloader data is not valid.
#define I2C_BTLDR_APP_STATUS_READY_MASK             (0x7f)
#define I2C_BTLDR_APP_STATUS_READY_NA               (0x00)
#define I2C_BTLDR_APP_STATUS_READY_READY            (0x01)
#define I2C_BTLDR_APP_STATUS_READY_CRC_FAILED       (0x02)
#define I2C_BTLDR_APP_STATUS_READY_NO_RESET         (0x03)
#define I2C_BTLDR_APP_STATUS_READY_RESV             (0x7f)
#define I2C_BTLDR_APP_STATUS_AUTOBOOT_MASK          (0x80)
#define I2C_BTLDR_APP_STATUS_AUTOBOOT_NO            (0x00)
#define I2C_BTLDR_APP_STATUS_AUTOBOOT_YES           (0x80)

// Keys are always sent MSB (KEY_3) first.
#define I2C_BTLDR_START_KEY_0                       (IOMCU_COMMON_KEY_0)
#define I2C_BTLDR_START_KEY_1                       (IOMCU_COMMON_KEY_1)
#define I2C_BTLDR_START_KEY_2                       (IOMCU_COMMON_KEY_2)
#define I2C_BTLDR_START_KEY_3                       (IOMCU_COMMON_KEY_3)

#define I2C_BTLDR_STATE_OK                          (0x00)
#define I2C_BTLDR_STATE_OVERFLOW                    (0x01)
#define I2C_BTLDR_STATE_UNDERFLOW                   (0x02)
#define I2C_BTLDR_STATE_INVALID_REG                 (0x03)
#define I2C_BTLDR_STATE_INVALID_I2C_SIZE            (0x04)
#define I2C_BTLDR_STATE_BAD_KEY                     (0x05)
#define I2C_BTLDR_STATE_INVALID_BLOCK_SIZE          (0x06)
#define I2C_BTLDR_STATE_INVALID_ADDR                (0x07)
#define I2C_BTLDR_STATE_ACCESS_DENIED               (0x08)
#define I2C_BTLDR_STATE_BUSY                        (0x09)
#define I2C_BTLDR_STATE_FLASH_INTERRUPTED           (0x0a)
#define I2C_BTLDR_STATE_CHECKSUM_FAILED             (0x0b)
#define I2C_BTLDR_STATE_DATA_NOT_READY              (0x0c)
#define I2C_BTLDR_STATE_FLASH_NOT_READY             (0x0d)
#define I2C_BTLDR_STATE_APP_NOT_READY               (0x0e)
#define I2C_BTLDR_STATE_COMM_ERROR                  (0xff)


// Not all of these are useful but added for quick reference.
// BtldrInfo and BtldrStatus can not change for backward compatibility.
typedef struct {
    uint32_t endian;
    uint16_t proto_ver;
    uint16_t btldr_ver;
} _S_PACKED BtldrInfo;

typedef struct {
    uint8_t status;
} _S_PACKED BtldrStatus;

typedef struct {
    uint16_t board_id;
    uint16_t board_rev;
    uint16_t chip_id;
    uint8_t chip_serial[12];
} _S_PACKED BtldrDeviceInfoV1;

typedef struct {
    uint16_t board_id;
    uint16_t board_rev;
    uint16_t chip_id;
    uint8_t chip_serial[12];
    uint8_t option_bytes[8];
} _S_PACKED BtldrDeviceInfo;

typedef union {
    BtldrDeviceInfoV1 v1;
    BtldrDeviceInfo v2;
    BtldrDeviceInfo current;
} BtldrDeviceInfoAll;

typedef struct {
    uint32_t eeprom_base;
    uint32_t eeprom_size;
    uint32_t data_base;
    uint32_t data_size;
    uint16_t eeprom_blocksize;
    uint16_t data_blocksize;
    uint16_t eeprom_read_time;
    uint16_t eeprom_write_time;
    uint16_t data_read_time;
    uint16_t data_write_time;
} _S_PACKED BtldrFlashInfo;

typedef struct {
    uint32_t size;
    uint16_t crc16;
    uint16_t app_ver;
    uint16_t app_info_set_time;
    uint8_t status;
} _S_PACKED BtldrAppInfo;

typedef IomcuKey BtldrStart;

typedef struct {
    uint32_t size;
    uint16_t crc16;
    uint16_t app_ver;
    uint8_t autoboot;
} _S_PACKED BtldrAppInfoSet;

typedef struct {
    uint32_t addr;
} _S_PACKED BtldrEEPROMReadAddr;

typedef struct {
    uint32_t addr;
} _S_PACKED BtldrDataReadAddr;

typedef struct {
    uint32_t addr;
} _S_PACKED BtldrEEPROMWriteAddr;

typedef struct {
    uint32_t addr;
} _S_PACKED BtldrDataWriteAddr;

typedef struct {
    uint32_t addr;
} _S_PACKED BtldrFlashRWAddr;


#endif  // IOMCU_BOOTLOADER_PROTO_H
