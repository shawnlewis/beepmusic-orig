#include <stdio.h>
#include <stdlib.h>

#include "i2c.h"

#define BUS 0
#define ADDR 0x23

uint8_t data[120];

void error() {
    fprintf(stderr, "i2c ERROR, make sure you're sudo'd\n");
    //exit(1);
}

int main(int argc, char** argv) {
    unsigned char rdata[120];
    unsigned char reg = 2;

    FILE *f = fopen("data.bin", "w");

    while (1) {
        while (1) {
            if (i2c_regread(BUS, ADDR, 4, data, 1)) {
                error();
            }
            if (data[0] == 1) {
                break;
            }
        }
        if (i2c_regread(BUS, ADDR, 2, data, 120)) {
            error();
        }
        fwrite(data, 120, 1, f);

        while (1) {
            if (i2c_regread(BUS, ADDR, 5, data, 1)) {
                error();
            }
            if (data[0] == 1) {
                break;
            }
        }
        if (i2c_regread(BUS, ADDR, 3, data, 120)) {
            error();
        }
        fwrite(data, 120, 1, f);
    }
}
