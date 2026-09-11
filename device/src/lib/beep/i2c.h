#include <stdint.h>

#include "linux/i2c.h"
#include "linux/i2c-dev.h"

enum {
    RW_ACK = 0,
    RW_NACK = 1,
    RW_ERROR_SEND = 2,
    RW_ERROR_BUS = 3,
    RW_ERROR_PARAM = 4
};

/* write and/or read i2c bus
* @param bus, I2c bus to work on
* @param addr, i2c device address
* @param wptr, pointer to data to write to
* @param wlen, write length
* @param rptr, pointer to store read data to
* @param rlen, number of bytes to read
* @return: 0=ack, 1=nack, 2=bus error, 3=parameter error
*/
int i2c_rw(int bus, int addr,
        uint8_t *wptr, int wlen,
        uint8_t *rptr, int rlen);

int i2c_regread(int bus, int addr,
        uint8_t reg,
        uint8_t *rptr, int rlen);
