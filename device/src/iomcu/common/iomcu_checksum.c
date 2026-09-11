#include "iomcu_checksum.h"


// simple crc8: poly = 0xd5, init = 0xff.
uint8_t crc8(uint8_t crc, uint8_t *buf, crclen_t len) {
    uint8_t i;
    while (len--) {
        crc ^= *buf++;
        for (i = 0; i < 8; i++) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0xd5;
            else
                crc <<= 1;
        }
    }
    return crc;
}

// simple crc16: poly = 0x8005, init = 0.
uint16_t crc16(uint16_t crc, uint8_t *buf, crclen_t len) {
    uint8_t i;
    while (len--) {
        crc ^= (((uint16_t)*buf++) << 8);
        for (i = 0; i < 8; i++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x8005;
            else
                crc <<= 1;
        }
    }
    return crc;
}

uint8_t i2c_message_checksum(uint8_t *buf, uint8_t regaddr, crclen_t len) {
    return crc8(IOMCU_CRC8_INIT + regaddr, buf, len);
}

#ifdef __GNUC__
int i2c_message_verify(uint8_t *buf, uint8_t regaddr, crclen_t len) {
#else
uint8_t i2c_message_verify(uint8_t *buf, uint8_t regaddr, crclen_t len) {
#endif
    return crc8(IOMCU_CRC8_INIT + regaddr, buf, len - 1) == buf[len - 1];
}
