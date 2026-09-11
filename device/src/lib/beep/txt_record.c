#include "beep/txt_record.h"

/*
 * Ensure you free() the txt_record_t this function returns when you no longer
 * need it!
 */
struct txt_record_t *tr_parse(const char *txtRecord, int txtLen) {
    struct txt_record_t *txt_record =
            calloc(1, sizeof(struct txt_record_t));

    char *ptr = (char *)txtRecord;
    int n=0;

    while(true) {
        if(ptr >= txtRecord + txtLen)
            break;

        unsigned char entry_len = (unsigned char) *(ptr++);

        char *entry = ptr;

        char *eq = strchr(entry, '=');
        if(eq == NULL) {
            return NULL;
        }

        int key_len = eq - entry;
        int val_len = entry_len - key_len - 1;

        char *key = entry;
        char *val = eq + 1;

        if(key == NULL
           || val == NULL
           || key_len > TR_KEY_MAX_LEN
           || val_len > TR_VAL_MAX_LEN) {
            return NULL;
        }

        struct txt_record_entry_t *txt_record_entry =
            &txt_record->entries[n++];

        memcpy(txt_record_entry->key, key, key_len);
        txt_record_entry->key[key_len] = 0;

        memcpy(txt_record_entry->val, val, val_len);
        txt_record_entry->val[val_len] = 0;

        ptr += entry_len;
    }

    return(txt_record);
}

char *tr_get_value(struct txt_record_t *txt_record, const char *key) {
    int i;
    for(i=0; i < TR_MAX_ENTRIES; i++) {
        if(!strcmp(txt_record->entries[i].key, key))
            return txt_record->entries[i].val;

        if(!strlen(txt_record->entries[i].key) ||
           !strlen(txt_record->entries[i].val))
            break;
    }

    return NULL;
}

char *tr_to_str(struct txt_record_t *txt_record) {
    char *str = calloc(1, TR_STR_MAX_LEN);
    int i;
    for(i=0; i < TR_MAX_ENTRIES; i++) {
        if(!strlen(txt_record->entries[i].key) ||
           !strlen(txt_record->entries[i].val))
            break;

        char *entry = calloc(1, TR_ENTRY_STR_MAX_LEN);
        unsigned char len =
            strlen(txt_record->entries[i].key) +
            strlen(txt_record->entries[i].val) +
            1;
        snprintf(entry, TR_ENTRY_STR_MAX_LEN, "%c%s=%s", len,
                txt_record->entries[i].key,
                txt_record->entries[i].val);

        strncat(str, entry, TR_STR_MAX_LEN);
    }

    return(str);
}

void tr_add_entry(struct txt_record_t *txt_record, const char *key,
        const char *val) {
    int i=0;
    while(strcmp(txt_record->entries[i].key, key) &&
          strlen(txt_record->entries[i].key) > 0)
        i++;

    strncpy(txt_record->entries[i].key, key, TR_KEY_MAX_LEN);
    strncpy(txt_record->entries[i].val, val, TR_VAL_MAX_LEN);
}
