#define _GNU_SOURCE  // Needed for getline on mips
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "beep/debug.h"

#include "jsonfile.h"

char* fd_to_path(int fd) {
    char* result = malloc(1024);
    char path[1024];
    sprintf(path, "/proc/self/fd/%d", fd);
    int n = readlink(path, result, 1024);
    if (n < 0) {
        LOG_ERROR(log_beep_main, "Couldn\'t read: %s", path);
        free(result);
        return NULL;
    }
    if (n > 1023) {
        n = 1023;
    }
    result[n] = '\0';
    return result;
}

BeepJsonFile* beep_jsonfile_init(const char* path) {
    BeepJsonFile* jsonfile = malloc(sizeof(BeepJsonFile));
    jsonfile->path = path;
    jsonfile->file = fopen(path, "a+");
    if (!jsonfile->file) {
        LOG_ERROR(log_beep_main, "Couldn\'t open %s. Reason %s\n",
                jsonfile->path, strerror(errno));
        free(jsonfile);
        return NULL;
    }

    char metadata_path[1024];
    sprintf(metadata_path, "%s.metadata", path);
    jsonfile->metadata_path = strdup(metadata_path);

    return jsonfile;
}

void beep_jsonfile_remove(const char* path) {
    remove(path);
    char metadata_path[1024];
    sprintf(metadata_path, "%s.metadata", path);
    remove(metadata_path);
}

int beep_jsonfile_num_rows(BeepJsonFile* jsonfile) {
    int i = 0;
    beep_jsonfile_foreach(row, jsonfile) {
        json_object_put(row);
        i++;
    }
    return i;
}

void beep_jsonfile_write_json_line(
        FILE* file, json_object* row) {
    char* row_s = strdup(json_object_to_json_string(row));
    int row_s_len = strlen(row_s) + 1;

    // Overwriting \0 with newline. Don't try to print this out!
    row_s[row_s_len - 1] = '\n';

    int num = fwrite(row_s, row_s_len, 1, file);
    if (num != 1) {
        LOG_ERROR(log_beep_main, "Couldn\'t write metadata to %s. Reason %s\n",
                fd_to_path(fileno(file)), strerror(errno));
        exit(1);
    }

    free(row_s);
}

void beep_jsonfile_add_row(BeepJsonFile* jsonfile, json_object* event) {
    beep_jsonfile_write_json_line(jsonfile->file, event);
    fflush(jsonfile->file);
}

json_object* beep_jsonfile_readline(FILE* file) {
    json_object* result = NULL;
    char* line = NULL;
    size_t len = 0;
    ssize_t numread = getline(&line, &len, file);
    if (numread == -1) {
        if (feof(file)) {
            clearerr(file);
        } else {
            LOG_ERROR(log_beep_main, "Error reading %s. Reason %s\n",
                    fd_to_path(fileno(file)), strerror(errno));
            exit(1);
        }
    } else {
        if (line[numread - 1] == '\n') {
            line[numread - 1] = '\0';
        }
        result = json_tokener_parse(line);
    }
    return result;
}

json_object* beep_jsonfile_next_row(BeepJsonFile* jsonfile) {
    return beep_jsonfile_readline(jsonfile->file);
}

json_object* beep_jsonfile_first_row(BeepJsonFile* jsonfile) {
    rewind(jsonfile->file);
    return beep_jsonfile_next_row(jsonfile);
}

json_object* beep_jsonfile_read_metadata(BeepJsonFile* jsonfile) {
    json_object* metadata = NULL;
    // Open file and read metadata if available.
    FILE* file = fopen(jsonfile->metadata_path, "r");
    if (file) {
        metadata = beep_jsonfile_readline(file);
        fclose(file);
    }
    if (!metadata) {
        metadata = json_object_new_object();
    }
    return metadata;
}

int beep_jsonfile_read_metadata_int(
        BeepJsonFile* jsonfile, const char* key, int* result) {
    json_object* metadata = beep_jsonfile_read_metadata(jsonfile);
    json_object* val_obj = json_object_object_get(metadata, key);
    if (!val_obj) {
        goto error;
    }
    if (json_object_get_type(val_obj) != json_type_int) {
        goto error;
    }
    *result = json_object_get_int(val_obj);

    json_object_put(metadata);
    return 0;

error:
    json_object_put(metadata);
    return -1;
}

void beep_jsonfile_write_metadata_int(
        BeepJsonFile* jsonfile, const char* key, int val) {
    json_object* metadata = beep_jsonfile_read_metadata(jsonfile);

    // Modify metadata
    json_object_object_add(metadata, key, json_object_new_int(val));

    // Write the metadata back out
    FILE* file = fopen(jsonfile->metadata_path, "w");
    if (!file) {
        LOG_ERROR(log_beep_main, "Couldn\'t open %s for write. Reason %s\n",
                jsonfile->metadata_path, strerror(errno));
        exit(1);
    }
    beep_jsonfile_write_json_line(file, metadata);

    json_object_put(metadata);
    fclose(file);
}
