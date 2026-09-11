#ifndef IOMCU_SHARED_PROTO_H
#define IOMCU_SHARED_PROTO_H


#ifdef __GNUC__
#include <stdint.h>
#define _S_PACKED   __attribute__((packed))
#else
#define _S_PACKED
#endif


#define IOMCU_BTLDR_RESV_EEPROM_BLOCKS              (1)
#define IOMCU_BTLDR_RESV_DATA_BLOCKS                (64)

#define IOMCU_I2C_NORM_SPEED                        (100000)
#define IOMCU_I2C_FAST_SPEED                        (400000)

#define IOMCU_KEY_SIZE                              (4)


// This key is always sent MSB (byte 3) first.
#define IOMCU_COMMON_KEY_0                          (0xa4)
#define IOMCU_COMMON_KEY_1                          (0xaa)
#define IOMCU_COMMON_KEY_2                          (0xaa)
#define IOMCU_COMMON_KEY_3                          (0xc0)
#define IOMCU_COMMON_KEY_CRC8_CHECKSUM              (0xed)


typedef struct {
    uint8_t key[4];
} _S_PACKED IomcuKey;


#endif  // IOMCU_SHARED_PROTO_H
