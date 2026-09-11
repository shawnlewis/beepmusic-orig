#include <check.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>

#include "beepupdate.h"
#include "machdefs.h"

static uint8_t *env_area;
static uint8_t *bc_area;
static uint8_t *scratch;

// Reads entire mtd file to scratch.
#define READ_MTD() \
    do { \
        FILE *__stream = fopen(BOOTCOUNT_DEV, "r"); \
        ck_assert_int_eq(fread(scratch, 1, BOOTCOUNT_OFFSET + \
                BOOTCOUNT_SIZE, __stream), BOOTCOUNT_OFFSET + \
                BOOTCOUNT_SIZE); \
        fclose(__stream); \
    } while (0)

// Writes mtd file from env_area and bc_area.
#define WRITE_MTD() \
    do { \
        FILE *__stream = fopen(BOOTCOUNT_DEV, "w"); \
        ck_assert_int_eq(fwrite(env_area, 1, BOOTCOUNT_OFFSET, __stream), \
                BOOTCOUNT_OFFSET); \
        ck_assert_int_eq(fwrite(bc_area, 1, BOOTCOUNT_SIZE, __stream), \
                BOOTCOUNT_SIZE); \
        fclose(__stream); \
    } while (0)

#define VERIFY_ENV(DATA) \
    ck_assert_int_eq(memcmp(DATA, env_area, BOOTCOUNT_OFFSET), 0)

#define VERIFY_BC(DATA) \
    ck_assert_int_eq(memcmp(DATA, bc_area, BOOTCOUNT_SIZE), 0)

// Uses scratch;
static void reset_bootcount_file(void) {
    FILE *stream;

    memset(scratch, 0xff, BOOTCOUNT_SIZE);
    stream = fopen(BOOTCOUNT_DEV, "w");
    fwrite(env_area, 1, BOOTCOUNT_OFFSET, stream);
    fwrite(scratch, 1, BOOTCOUNT_SIZE, stream);
    fclose(stream);
}

START_TEST(bootcount_test) {
    uint8_t *p;
    int i;
    int j;
    uint8_t before_good[] = {0x7f, 0x3f, 0x1f, 0x07, 0x03, 0x01};
    uint8_t after_good[] = {0x0f, 0x0f, 0x0f, 0x00, 0x00, 0x00};

    reset_bootcount_file();
    memset(bc_area, 0xff, BOOTCOUNT_SIZE);

    // Fully reset bootcount area.
    ck_assert_int_eq(clear_bootcount(), UPDATE_OK);
    READ_MTD();
    VERIFY_ENV(scratch);

    // Check second nibble for already clear.
    bc_area[0] = 0x0f;
    WRITE_MTD();
    ck_assert_int_eq(clear_bootcount(), UPDATE_OK);
    READ_MTD();
    VERIFY_ENV(scratch);

    // Check known good values for first 8 boots (4 bytes).
    for (i = 0; i < 4; i++) {
        if (i)
            bc_area[i - 1] = 0;

        for (j = 0; j < 6; j++) {
            bc_area[i] = before_good[j];
            WRITE_MTD();
            bc_area[i] = after_good[j];
            ck_assert_int_eq(clear_bootcount(), UPDATE_OK);
            READ_MTD();
            VERIFY_ENV(scratch);
            VERIFY_BC(scratch + BOOTCOUNT_OFFSET);
        }
    }

    // Check for some bad values.
    // It is not intuitive that the second nibble will always be written
    // back as 0xf since for flash this does nothing.  We have to step
    // through each nibble separately.
    bc_area[4] = 0xef;
    WRITE_MTD();
    ck_assert_int_eq(clear_bootcount(), UPDATE_OK);
    READ_MTD();
    bc_area[4] = 0x0f;
    VERIFY_ENV(scratch);
    VERIFY_BC(scratch + BOOTCOUNT_OFFSET);

    bc_area[4] = 0x05;
    WRITE_MTD();
    ck_assert_int_eq(clear_bootcount(), UPDATE_OK);
    READ_MTD();
    bc_area[4] = 0x00;
    VERIFY_ENV(scratch);
    VERIFY_BC(scratch + BOOTCOUNT_OFFSET);

    bc_area[5] = 0x2f;
    WRITE_MTD();
    ck_assert_int_eq(clear_bootcount(), UPDATE_OK);
    READ_MTD();
    bc_area[5] = 0x0f;
    VERIFY_ENV(scratch);
    VERIFY_BC(scratch + BOOTCOUNT_OFFSET);

    bc_area[5] = 0x09;
    WRITE_MTD();
    ck_assert_int_eq(clear_bootcount(), UPDATE_OK);
    READ_MTD();
    bc_area[5] = 0x00;
    VERIFY_ENV(scratch);
    VERIFY_BC(scratch + BOOTCOUNT_OFFSET);

    // Test bc reset.
    // Set entire space to 0s except the last 4 bytes.
    memset(bc_area, 0, BOOTCOUNT_SIZE - 4);

    // Move through the last 3 bytes.
    for (i = 0; i < 3; i++) {
        bc_area[BOOTCOUNT_SIZE - 4 + i - 1] = 0;
        bc_area[BOOTCOUNT_SIZE - 4 + i] = 0x7f;
        WRITE_MTD();
        bc_area[BOOTCOUNT_SIZE - 4 + i] = 0x0f;
        ck_assert_int_eq(clear_bootcount(), UPDATE_OK);
        READ_MTD();
        VERIFY_ENV(scratch);
        VERIFY_BC(scratch + BOOTCOUNT_OFFSET);
        bc_area[BOOTCOUNT_SIZE - 4 + i] = 0x07;
        WRITE_MTD();
        bc_area[BOOTCOUNT_SIZE - 4 + i] = 0x00;
        ck_assert_int_eq(clear_bootcount(), UPDATE_OK);
        READ_MTD();
        VERIFY_ENV(scratch);
        VERIFY_BC(scratch + BOOTCOUNT_OFFSET);
    }

    // After the first nibble in the last byte is detected the bootcount
    // area should be reset back to 0xff.
    bc_area[BOOTCOUNT_SIZE - 1] = 0x7f;
    WRITE_MTD();
    ck_assert_int_eq(clear_bootcount(), UPDATE_OK);
    READ_MTD();
    VERIFY_ENV(scratch);
    i = BOOTCOUNT_SIZE;
    p = scratch + BOOTCOUNT_OFFSET;
    while (i--) {
        if (*p++ != 0xff)
            break;
    }
    ck_assert_int_eq(i, -1);

    // Check the same but with the second nibble as the current bootcount,
    // though this shouldn't happen.
    bc_area[BOOTCOUNT_SIZE - 1] = 0x07;
    WRITE_MTD();
    ck_assert_int_eq(clear_bootcount(), UPDATE_OK);
    READ_MTD();
    VERIFY_ENV(scratch);
    i = BOOTCOUNT_SIZE;
    p = scratch + BOOTCOUNT_OFFSET;
    while (i--) {
        if (*p++ != 0xff)
            break;
    }
    ck_assert_int_eq(i, -1);

    // Check the same but with a completely spent bootcount area, again this
    // shouldn't happen but also takes a different code path from the above
    // two cases.
    bc_area[BOOTCOUNT_SIZE - 1] = 0x00;
    WRITE_MTD();
    ck_assert_int_eq(clear_bootcount(), UPDATE_OK);
    READ_MTD();
    VERIFY_ENV(scratch);
    i = BOOTCOUNT_SIZE;
    p = scratch + BOOTCOUNT_OFFSET;
    while (i--) {
        if (*p++ != 0xff)
            break;
    }
    ck_assert_int_eq(i, -1);
}
END_TEST

Suite *bootcount_suite(void) {
    FILE *stream;

    env_area = malloc(BOOTCOUNT_OFFSET);
    bc_area = malloc(BOOTCOUNT_SIZE);
    scratch = malloc(BOOTCOUNT_OFFSET + BOOTCOUNT_SIZE);
    assert(env_area && bc_area && scratch);

    if (check_access(BOOTCOUNT_DEV, F_OK) == UPDATE_OK) {
        fprintf(stderr, "%s file already exists\n", BOOTCOUNT_DEV);
        exit(2);
    }

    stream = fopen("/dev/urandom", "r");
    fread(env_area, 1, BOOTCOUNT_OFFSET, stream);
    fclose(stream);

    reset_bootcount_file();

    Suite *s = suite_create("bootcount");

    TCase *tc_core = tcase_create("core");
    tcase_add_test(tc_core, bootcount_test);
    suite_add_tcase(s, tc_core);

    return s;
}

void cleanup_bootcount_suite(void) {
    if (env_area)
        free(env_area);
    if (bc_area)
        free(bc_area);
    if (scratch)
        free(scratch);
    unlink(BOOTCOUNT_DEV);
}
