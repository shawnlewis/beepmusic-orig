#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ioutil.h"


#ifdef _MIPS_ARCH
// From: laulib/i2c.c
// TODO: This should go through a hal and also be part of the shared codebase.
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
        packets.msgs = &messages[0];    // Write and read.
        packets.nmsgs = 2;
    } else if(wlen > 0) {
        packets.msgs = &messages[0];    // Only write.
        packets.nmsgs = 1;
    } else if(rlen > 0) {
        packets.msgs = &messages[1];    // Only read.
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

    // Check for error from i2c transfer.
    if ((messages[0].flags & I2C_M_NO_RD_ACK)
            || (messages[1].flags & I2C_M_NO_RD_ACK)){
        return RW_NACK;
    } else {
        return RW_ACK;
    }
}

int i2c_reg_read(int bus, int addr, uint8_t regaddr, uint8_t *buf, int len) {
    return i2c_rw(bus, addr, &regaddr, 1, buf, len);
}

int i2c_reg_write(int bus, int addr, uint8_t regaddr, uint8_t *buf, int len) {
    uint8_t *tbuf = malloc(sizeof(uint8_t) * (len + 1));
    int ret;
    if (!tbuf)
        return RW_ERROR_NOMEM;
    tbuf[0] = regaddr;
    memcpy(tbuf + 1, buf, len);
    ret = i2c_rw(bus, addr, tbuf, len + 1, NULL, 0);
    free(tbuf);
    return ret;
}
#else
/// Fake data for testing.
#include <assert.h>

int i2c_reg_read(int bus, int addr, uint8_t regaddr, uint8_t *buf, int len) {
    FILE *f = fopen("/dev/urandom", "r");
    assert(f);
    size_t s = fread(buf, 1, len, f);
    assert(s == len);
    fclose(f);
    return RW_ACK;
}

int i2c_reg_write(int bus, int addr, uint8_t regaddr, uint8_t *buf, int len) {
    return RW_ACK;
}
#endif
