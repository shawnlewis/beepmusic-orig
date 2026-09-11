#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// TODO: Use lib/beep/i2c.c
#include "ioutil.h"


#define DEFAULT_I2C_BUS                             (0)
#define I2C_APP_ADDR                                (0x23)

void error(void) {
    fprintf(stderr, "i2c ERROR, make sure you're sudo'd\n");
    //exit(1);
}

static void write_data(FILE *f, uint8_t *cdata) {
    uint8_t data[120];
    int i;

    for (i = 0; i < 30; i++) {
        int c_off = i * 3;
        int d_off = i * 4;
        data[d_off] = cdata[c_off] & 0xfc;
        data[d_off + 1] = (cdata[c_off] << 6)
                | ((cdata[c_off + 1] & 0xf0) >> 2);
        data[d_off + 2] = (cdata[c_off + 1] << 4)
                | ((cdata[c_off + 2] & 0xc0) >> 4);
        data[d_off + 3] = (cdata[c_off + 2] << 2);
    }
    fwrite(data, 120, 1, f);
}

int main(int argc, char** argv) {
    uint8_t cdata[90];

    FILE *f = fopen("data.bin", "w");

    while (1) {
        while (1) {
            if (i2c_reg_read(DEFAULT_I2C_BUS, I2C_APP_ADDR, 8, cdata, 1)) {
                error();
            }
            if (cdata[0] == 1) {
                break;
            }
        }
        if (i2c_reg_read(DEFAULT_I2C_BUS, I2C_APP_ADDR, 6, cdata, 90)) {
            error();
        }
        write_data(f, cdata);

        while (1) {
            if (i2c_reg_read(DEFAULT_I2C_BUS, I2C_APP_ADDR, 9, cdata, 1)) {
                error();
            }
            if (cdata[0] == 1) {
                break;
            }
        }
        if (i2c_reg_read(DEFAULT_I2C_BUS, I2C_APP_ADDR, 7, cdata, 90)) {
            error();
        }
        write_data(f, cdata);
    }
}
