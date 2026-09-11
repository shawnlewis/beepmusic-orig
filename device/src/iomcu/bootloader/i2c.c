#include "stm8l15x.h"

#include "iomcu_checksum.h"
#include "bootloader.h"
#include "bootloader_proto.h"

#define I2C_START_RECV                          (0)
#define I2C_START_TRA                           (1)

#define I2C_STATE_RESET()                       \
    do { \
        if (i2c_state != I2C_STATE_IDLE) { \
            i2c_state = I2C_STATE_IDLE; \
            i2c_offset = 0; \
            i2c_out_remaining = 0; \
            i2c_reg = I2C_BTLDR_REG_RESV; \
        } \
    } while (0)

uint8_t i2c_state = I2C_STATE_IDLE;
uint8_t i2c_buf[I2C_BUF_SIZE + 2];
uint8_t i2c_offset;
uint8_t i2c_out_remaining;
uint8_t i2c_reg = I2C_BTLDR_REG_RESV;


void i2c_message_start(uint8_t tra) {
    switch (i2c_state) {
    case I2C_STATE_IDLE:
        if (tra == I2C_START_RECV) {
            i2c_state = I2C_STATE_REG_WAIT;
        }
        break;

    case I2C_STATE_OUT_START_WAIT:
        if (tra == I2C_START_TRA) {
            uint8_t checksum;
            // Perform read commands here.
            cmd_data_out(i2c_reg);

            // Create checksum for outgoing message.
            checksum = i2c_message_checksum(&i2c_buf[i2c_offset],
                    i2c_reg, i2c_out_remaining);
            i2c_buf[i2c_offset + i2c_out_remaining] = checksum;

            // Add checksum byte and trailing readout byte.
            i2c_out_remaining += 2;

            i2c_state = I2C_STATE_DATA_OUT;
            break;
        }
        // No break here for state reset.

    default:
        I2C_STATE_RESET();
    }
}

void i2c_message_stop(void) {
    switch (i2c_state) {
    case I2C_STATE_DATA_IN:
        // Check incoming message checksum.
        if (i2c_message_verify(i2c_buf, i2c_reg, i2c_offset)) {
            // Remove checksum byte.
            i2c_offset--;

            // Perform write commands here.
            cmd_data_in(i2c_reg);
        } else {
            btldr_state = I2C_BTLDR_STATE_CHECKSUM_FAILED;
            flash_state = FLASH_STATE_IDLE;
        }
        // Fall through to reset i2c state.
    case I2C_STATE_DATA_OUT:
    default:
        I2C_STATE_RESET();
    }
}

void i2c_byte_in(uint8_t byte) {
    switch (i2c_state) {
    case I2C_STATE_REG_WAIT:
        if (byte != I2C_BTLDR_REG_RESV) {
            // If the bootloader is not in the _RUN state set all registers
            // to I2C_BTLDR_REG_RESV except for I2C_BTLDR_REG_START.  This
            // allows bootloader to be on the bus but only to wait for the
            // start key.
            if (run_state == RUN_STATE_BTLDR_RUN
                    || byte == I2C_BTLDR_REG_START) {
                i2c_reg = byte;
            } else {
                // Set appropriate read/write bit.
                i2c_reg = I2C_BTLDR_REG_RESV_2
                        | (byte & I2C_BTLDR_REG_RW_MASK);
            }
            if ((i2c_reg & I2C_BTLDR_REG_RW_MASK) == I2C_BTLDR_REG_READ) {
                i2c_state = I2C_STATE_OUT_START_WAIT;
            } else {
                i2c_state = I2C_STATE_DATA_IN;
            }
        } else {
            I2C_STATE_RESET();
        }
        break;

    case I2C_STATE_DATA_IN:
        if (i2c_offset < I2C_BUF_SIZE) {
            i2c_buf[i2c_offset] = byte;
            i2c_offset++;
        } else {
            btldr_state = I2C_BTLDR_STATE_OVERFLOW;
        }
        break;

    default:
        I2C_STATE_RESET();
    }
}

uint8_t i2c_byte_out(void) {
    uint8_t byte = 0x55;

    switch (i2c_state) {
    case I2C_STATE_DATA_OUT:
        if (i2c_out_remaining && i2c_offset < I2C_BUF_SIZE) {
            byte = i2c_buf[i2c_offset];
            i2c_offset++;
            i2c_out_remaining--;
        } else {
            if (i2c_out_remaining) {
                // Overflow while reading indicates a programming error in
                // the bootloader.
                btldr_state = I2C_BTLDR_STATE_OVERFLOW;
            } else {
                btldr_state = I2C_BTLDR_STATE_UNDERFLOW;
            }
        }
        break;

    default:
        I2C_STATE_RESET();
    }

    return byte;
}

uint16_t i2c_proc_event(uint16_t timeout) {
    uint8_t sr1;
    uint8_t sr2;
    __IO uint8_t sr3;

    if (timeout) {
        // Wait for an I2C event or timeout.
        do {
            sr2 = I2C1->SR2;
            sr1 = I2C1->SR1;
        } while (!sr1 && !sr2 && --timeout);

        if (!timeout)
            return 0;
    } else {
        // Wait for an I2C event.
        do {
            sr2 = I2C1->SR2;
            sr1 = I2C1->SR1;
        } while (!sr1 && !sr2);
    }

    // Read SR3 last since it can clear I2C_SR1_ADDR.
    sr3 = I2C1->SR3;

    // Comm error.
    if (sr2 & (I2C_SR2_WUFH | I2C_SR2_OVR |I2C_SR2_ARLO |I2C_SR2_BERR)) {
        I2C1->CR2 |= I2C_CR2_STOP;  // stop communication - release the lines.
        I2C1->SR2 = 0;  // clear all error flags.
    }
    //// Bytes recieved, byte transfer finished.
    //if ((sr1 & (I2C_SR1_RXNE | I2C_SR1_BTF)) ==
    //        (I2C_SR1_RXNE | I2C_SR1_BTF)) {
    //    i2c_byte_in(I2C1->DR);
    //}
    // Bytes recieved.
    if (sr1 & I2C_SR1_RXNE) {
        i2c_byte_in(I2C1->DR);
    }
    // ACK failure.
    if (sr2 & I2C_SR2_AF) {
        I2C1->SR2 &= ~I2C_SR2_AF;  // clear AF.
        i2c_message_stop();
    }
    // Stop bit from master.
    if (sr1 & I2C_SR1_STOPF) {
        I2C1->CR2 |= I2C_CR2_ACK;  // CR2 write to clear STOPF.
        i2c_message_stop();
    }
    // Address matched.
    if (sr1 & I2C_SR1_ADDR) {
        i2c_message_start(((sr3 & I2C_SR3_TRA) == I2C_SR3_TRA)
                ? I2C_START_TRA : I2C_START_RECV);
    }
    // Ready to send, byte transfer finished.
    if ((sr1 & (I2C_SR1_TXE | I2C_SR1_BTF)) == (I2C_SR1_TXE | I2C_SR1_BTF)) {
        I2C1->DR = i2c_byte_out();
    }
    // Ready to send.
    if (sr1 & I2C_SR1_TXE) {
        I2C1->DR = i2c_byte_out();
    }

    // Return timeout remaining.
    return timeout;
}
