/*
 * Modified blobmsg-example.c for inspecting arbitrary blobmsg dump files
 */

#include <stdio.h>

#include <libubox/blobmsg.h>
#include <inttypes.h>

static const char *indent_str = "\t\t\t\t\t\t\t\t\t\t\t\t\t";

#define indent_printf(indent, ...) do { \
    if (indent > 0) \
        fwrite(indent_str, indent, 1, stderr); \
    fprintf(stderr, __VA_ARGS__); \
} while(0)

static void dump_attr_data(void *data, int len, int type, int indent, int next_indent);

static void
dump_table(struct blob_attr *head, int len, int indent, bool array) {
    struct blob_attr *attr;
    struct blobmsg_hdr *hdr;
    int n=0;

    fprintf(stderr, "%s {", array ? "ARRAY" : "TABLE" );
    __blob_for_each_attr(attr, head, len) {
        if(!n) fprintf(stderr, "\n");
        hdr = blob_data(attr);
        if(!array)
            indent_printf(indent + 1, "%s : ", hdr->name);
        else
            indent_printf(indent + 1, "%d : ", n);

        n++;

        dump_attr_data(blobmsg_data(attr), blobmsg_data_len(attr), blob_id(attr), 0, indent + 1);
    }
    indent_printf(indent, "}\n");
}

static void
dump_attr_data(void *data, int len, int type, int indent, int next_indent) {
    switch(type) {
    case BLOBMSG_TYPE_STRING:
        indent_printf(indent, "STRING \"%s\"\n", (char *) data);
        break;
    case BLOBMSG_TYPE_INT8:
        indent_printf(indent, "INT8 %d\n", *(uint8_t *)data);
        break;
    case BLOBMSG_TYPE_INT16:
        indent_printf(indent, "INT16 %d\n", be16_to_cpu(*(uint16_t *)data));
        break;
    case BLOBMSG_TYPE_INT32:
        indent_printf(indent, "INT32 %d\n", be32_to_cpu(*(uint32_t *)data));
        break;
    case BLOBMSG_TYPE_INT64:
        indent_printf(indent, "INT64 %"PRId64"\n", be64_to_cpu(*(uint64_t *)data));
        break;
    case BLOBMSG_TYPE_TABLE:
    case BLOBMSG_TYPE_ARRAY:
        dump_table(data, len, next_indent, type == BLOBMSG_TYPE_ARRAY);
        break;
    }
}

int main(int argc, char **argv)
{
    struct blob_attr *attr;
    FILE *f;

    if(argc != 2) {
        fprintf(stderr, "Usage: %s <blobmsg_dump>\n", argv[0]);
        return -1;
    }

    f = fopen(argv[1], "rb");
    if(!f) {
        fprintf(stderr, "Couldn't open file %s\n", argv[1]);
        return -1;
    }

    fseek(f, 0L, SEEK_END);
    size_t len = ftell(f);
    fseek(f, 0L, SEEK_SET);

    attr = malloc(len);
    if(!attr) {
        fprintf(stderr, "Can't allocate %zu bytes to hold contents of %s\n",
                len, argv[1]);
        fclose(f);
        return -1;
    }

    if(fread((char *)attr, 1, len, f) < len) {
        fprintf(stderr, "Failed to read %s: %s\n",
                argv[1], strerror(errno));
        fclose(f);
        return -1;
    }

    fclose(f);
    dump_table(blob_data(attr), blob_len(attr), 0, blob_id(attr) == BLOBMSG_TYPE_ARRAY);
    free(attr);

    return 0;
}
