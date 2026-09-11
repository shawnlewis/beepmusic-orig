#include <assert.h>
#include <stdio.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "registers.h"


void* map_page(uint32_t address) {
    assert(address % PAGE_SIZE == 0);
    int memfd = open("/dev/mem", O_RDWR | O_SYNC);
    if(memfd == -1) {
        printf("Couldn\'t open /dev/mem\n");
        exit(1);
    }

    void* virt_page = mmap(
            0, PAGE_SIZE, PROT_READ | PROT_WRITE,
            MAP_SHARED, memfd, address);

    return virt_page;
}

uint32_t read_word(void* page, uint32_t offset) {
    return *((uint32_t*) (page + offset));
}

void write_word(void* page, uint32_t offset, uint32_t val) {
    *((uint32_t*) (page + offset)) = val;
}

void clear_bits(void* page, uint32_t offset, uint32_t mask) {
    uint32_t val = read_word(page, offset);
    write_word(page, offset, val & ~mask);
}

void set_bits(void* page, uint32_t offset, uint32_t mask) {
    uint32_t val = read_word(page, offset);
    write_word(page, offset, val | mask);
}

void print_word(uint32_t word) {
    printf("0x%X\n", word);
}

void print_word_as_bits(uint32_t word) {
    for (int i=31; i>=0; i--) {
        if ((1 << i) & word) {
            printf("1");
        } else {
            printf("0");
        }
    }
    printf("\n");
}

void print_bit_labels() {
    printf("   |   |   |   |   |   |   |   |\n");
    printf("  28  24  20  16  12   8   4   0\n");
}

void* init(void) {
    void* page = map_page(GPIO_BASE_ADDRESS);

    // disable jumpstart on gpio11 and wps on gpio12
    set_bits(page, GPIO_FUNCTION_2, JUMPSTART_DISABLE_BIT | WPS_DISABLE_BIT);

    return page;
}

bool is_input(int gpio) {
    for (int i = 0; i < NUM_INPUT_OUTPUTS; i++) {
        if (INPUT_OUTPUTS[i] == gpio) {
            return true;
        }
    }
    return false;
}

bool is_output(int gpio) {
    // All inputs can be outputs
    if (is_input(gpio)) {
        return true;
    }
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        if (OUTPUTS[i] == gpio) {
            return true;
        }
    }
    return false;
}

bool set_output_enable(void* page, int gpio, bool enable) {
    if (enable && is_output(gpio)) {
        set_bits(page, GPIO_OE, 1 << gpio);
        return true;
    } else if (!enable && is_input(gpio)) {
        clear_bits(page, GPIO_OE, 1 << gpio);
        return true;
    } else {
        return false;
    }
}
