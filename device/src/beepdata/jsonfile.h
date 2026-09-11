#ifndef BEEP_JSONFILE_H
#define BEEP_JSONFILE_H

#include <json.h>
#include <stdio.h>

#include "beep/debug.h"

typedef struct {
    const char* path;
    FILE* file;
    const char* metadata_path;
} BeepJsonFile;

BeepJsonFile* beep_jsonfile_init(const char* path);
int beep_jsonfile_read_metadata_int(
        BeepJsonFile* jsonfile, const char* key, int* result);
void beep_jsonfile_write_metadata_int(
        BeepJsonFile* jsonfile, const char* key, int val);
void beep_jsonfile_add_row(BeepJsonFile* jsonfile, json_object* event);
json_object* beep_jsonfile_next_row(BeepJsonFile* jsonfile);
json_object* beep_jsonfile_first_row(BeepJsonFile* jsonfile);
#define beep_jsonfile_foreach(it_name, j) \
    for (json_object* it_name = beep_jsonfile_first_row(j); \
            it_name; \
            it_name = beep_jsonfile_next_row(j))

// For testing
void beep_jsonfile_remove(const char* path);
int beep_jsonfile_num_rows(BeepJsonFile* jsonfile);

#endif  // BEEP_JSONFILE_H
