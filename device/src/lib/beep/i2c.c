#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "i2c.h"

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
        uint8_t *rptr, int rlen) {
    int f;
    struct i2c_rdwr_ioctl_data packets;
    struct i2c_msg messages[2];
    char dev[15];

    if (wlen == 0 && rlen == 0) {
        return RW_ERROR_PARAM;
    }

    sprintf(&dev[0], "/dev/i2c-%d", bus);

    messages[0].addr  = addr;
    messages[0].flags = 0;
    messages[0].len   = wlen;
    messages[0].buf   = (uint8_t *) wptr;

    messages[1].addr  = addr;
    messages[1].flags = I2C_M_RD;
    messages[1].len   = rlen;
    messages[1].buf   = (uint8_t*) rptr;

    if (wlen > 0 && rlen > 0) {
        packets.msgs = &messages[0];    //write & read
        packets.nmsgs = 2;
    } else if(wlen > 0) {
        packets.msgs = &messages[0];    //only write
        packets.nmsgs = 1;
    } else if(rlen > 0) {
        packets.msgs = &messages[1];    //only read
        packets.nmsgs = 1;
    }

    if ((f = open(dev, O_RDWR)) < 0) {
        return RW_ERROR_BUS;
    }

    if (ioctl(f, I2C_RDWR, &packets) < 0) {
        close(f);
        return RW_ERROR_SEND;
    }

    close(f);

    /* check for error from i2c transfer */
    if ((messages[0].flags & I2C_M_NO_RD_ACK)
            || (messages[1].flags & I2C_M_NO_RD_ACK)){
        return RW_NACK;
    } else {
        return RW_ACK;
    }
}

int i2c_regread(int bus, int addr,
        uint8_t reg,
        uint8_t *rptr, int rlen) {
    return i2c_rw(bus, addr, &reg, sizeof(reg), rptr, rlen);
}
