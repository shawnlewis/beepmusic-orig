#include <stdio.h>

#include "beepcomm.h"

struct _header_field {
    char name[HEADER_FIELD_NAME_LEN];
    char value[HEADER_FIELD_VALUE_LEN];
};

struct _header {
    struct _header_field **fields;
    int n_fields;
};

header_t *header_parse(const char *buf) {
    const char *line;
    const char *line_end;
    const char *sep;

    struct _header *hdr;

    // Leader line check
    if(strstr(buf, "CASTCHAT/1.0\n") != buf) {
        return NULL;
    }

    hdr = calloc(1, sizeof(struct _header));

    line = buf + 13; // Skip leader line
    line_end = strchr(line, '\n');
    while(line_end && line_end != line) {
        sep = strnchr(line, ':', line_end - line);
        if(sep) {
            struct _header_field *f = calloc(1, sizeof(struct _header_field));

            memcpy(f->name, line, sep - line);
            strtrim(f->name);

            memcpy(f->value, sep + 1, line_end - sep - 1);
            strtrim(f->value);

            hdr->fields = realloc(hdr->fields,
                    sizeof(struct _header_field *) * (hdr->n_fields + 1));
            hdr->fields[hdr->n_fields] = f;
            hdr->n_fields++;
        }

        line = line_end + 1;
        line_end = strchr(line, '\n');
    }

    return (header_t *)hdr;
}

const char *header_get(header_t *hdr, const char *name) {
    int i;

    if(!hdr || !name) {
        return NULL;
    }

    for(i = 0; i < hdr->n_fields; i++) {
        if(!strcasecmp(hdr->fields[i]->name, name)) {
            return hdr->fields[i]->value;
        }
    }

    return NULL;
}

void header_free(header_t *hdr) {
    int i;

    if(!hdr) {
        return;
    }

    for(i = 0; i < hdr->n_fields; i++) {
        free(hdr->fields[i]);
    }

    free(hdr->fields);
    free(hdr);
}
