#ifndef IOMCU_CHECKSUM_H
#define IOMCU_CHECKSUM_H


#define IOMCU_CRC8_INIT                             (0xff)
#define IOMCU_CRC16_INIT                            (0x0000)


#ifdef __GNUC__
#include <stdint.h>
typedef int crclen_t;

uint8_t crc8(uint8_t crc, uint8_t *buf, crclen_t len);
uint16_t crc16(uint16_t crc, uint8_t *buf, crclen_t len);
uint8_t i2c_message_checksum(uint8_t *buf, uint8_t regaddr, crclen_t len);
int i2c_message_verify(uint8_t *buf, uint8_t regaddr, crclen_t len);

#else  // __ICCSTM8__ or __IAR_SYSTEMS_ICC__
#include <stm8l15x.h>

typedef uint8_t crclen_t;

uint8_t crc8(uint8_t crc, uint8_t *buf, crclen_t len);
uint16_t crc16(uint16_t crc, uint8_t *buf, crclen_t len);
uint8_t i2c_message_checksum(uint8_t *buf, uint8_t regaddr, uint8_t len);
uint8_t i2c_message_verify(uint8_t *buf, uint8_t regaddr, uint8_t len);
#endif


#endif  // IOMCU_CHECKSUM_H
