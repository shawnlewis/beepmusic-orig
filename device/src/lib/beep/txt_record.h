#ifndef TXT_RECORD_H
#define TXT_RECORD_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define TR_MAX_ENTRIES 20
#define TR_KEY_MAX_LEN 128
#define TR_VAL_MAX_LEN 128
#define TR_STR_MAX_LEN 1024
#define TR_ENTRY_STR_MAX_LEN (TR_KEY_MAX_LEN + TR_VAL_MAX_LEN + 1)

struct txt_record_entry_t {
    char key[TR_KEY_MAX_LEN];
    char val[TR_VAL_MAX_LEN];
};

struct txt_record_t {
    struct txt_record_entry_t entries[TR_MAX_ENTRIES];
};

struct txt_record_t *tr_parse(const char *txtRecord, int txtLen);
char *tr_get_value(struct txt_record_t *txt_record, const char *key);
char *tr_to_str(struct txt_record_t *txt_record);
void tr_add_entry(struct txt_record_t *txt_record, const char *key,
        const char *val);

#endif
