#include <stdbool.h>
#include <stdio.h>

#include "libubox/blobmsg_json.h"

char buf[1024 * 1024];

int main(int argc, char** argv) {
    char *fname = argv[1];
    FILE *f = fopen(fname, "r");
    size_t n = fread(buf, 1, 1024 * 1024, f);
    printf("Read %d chars\n", n);

    char *json = blobmsg_format_json((struct blob_attr *) buf, false);
    printf("JSON: %s\n", json);

    return 0;
}
