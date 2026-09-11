#ifndef IOUTIL_H
#define IOUTIL_H

#include <stdint.h>
#include <stdbool.h>


// From: laulib/i2c.c
// TODO: This should go through a hal and also be part of the shared codebase.
enum {
    RW_ACK = 0,
    RW_NACK = 1,
    RW_ERROR_SEND = 2,
    RW_ERROR_BUS = 3,
    RW_ERROR_PARAM = 4,
    RW_ERROR_NOMEM = 5
};

int i2c_rw(int bus, int addr,
        uint8_t *wptr, int wlen,
        uint8_t *rptr, int rlen);
int i2c_reg_read(int bus, int addr, uint8_t regaddr, uint8_t *buf, int len);
int i2c_reg_write(int bus, int addr, uint8_t regaddr, uint8_t *buf, int len);


extern bool en_dprintf;

#ifdef NDPRINTF
#define DPRINTF(__FMT__, ...)
#else
#define DPRINTF(__FMT__, ...) \
    if (en_dprintf) fprintf(stderr, __FMT__, ## __VA_ARGS__)
#endif


#endif  // IOUTIL_H
