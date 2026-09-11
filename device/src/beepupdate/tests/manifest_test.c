#include <assert.h>
#include <check.h>
#include <fcntl.h>
#include <unistd.h>

#include "beepupdate.h"

// Note: vim will add a new line to the end of files unless using
// :set binary :set noeol.

static char empty_manifest[] = \
    "# 0:5f2dfae87751a45a9e27f9a551a57892\n"
    "---\n"
    "manifest:\n"
    "  millis: 0\n"
    "files:\n";
static size_t empty_manifest_size = sizeof(empty_manifest) - 1;

static char simple_manifest[] = \
    "# 0:02ebf458944ae824c6d15b99e3c79125\n"
    "---\n"
    "manifest:\n"
    "  millis: 1381285389778\n"
    "files:\n"
    "- path: /path/to/a\n"
    "  dig: 0:d41d8cd98f00b204e9800998ecf8427e\n"
    "  type: file\n"
    "- path: /path/to/b\n"
    "  type: rm\n"
    "  flags: 0x00000007\n"
    "- path: /path/to/c\n"
    "  dig: 0:d41d8cd98f00b204e9800998ecf8427e\n"
    "  type: sh\n"
    "- path: /path/to/d\n"
    "  dig: 0:d41d8cd98f00b204e9800998ecf8427e\n"
    "  type: lua\n"
    "- path: /path/to/e\n"
    "  dig: 0:d41d8cd98f00b204e9800998ecf8427e\n"
    "  type: pfirm\n"
    "- path: /path/to/f\n"
    "  dig: 0:d41d8cd98f00b204e9800998ecf8427e\n"
    "  type: rfirm\n";
static size_t simple_manifest_size = sizeof(simple_manifest) - 1;

static char *simple_manifest_paths[] = {
    "/path/to/a",
    "/path/to/b",
    "/path/to/c",
    "/path/to/d",
    "/path/to/e",
    "/path/to/f"
};

static uint32_t simple_manifest_flags[] = {
    0x00000000,
    0x00000007,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000
};

static UMItemType simple_manifest_types[] = {
    UMITEM_FILE,
    UMITEM_REMOVE,
    UMITEM_SH_SCRIPT,
    UMITEM_LUA_SCRIPT,
    UMITEM_PRI_FIRMWARE,
    UMITEM_REC_FIRMWARE
};

#define SIMPLE_MANIFEST_COUNT                       (6)

static char simple_manifest_swap[] = \
    "# 0:b5d78accbbb84b32ca5e6e9cda84132b\n"
    "---\n"
    "files:\n"
    "- path: /path/to/a\n"
    "  type: file\n"
    "  dig: 0:d41d8cd98f00b204e9800998ecf8427e\n"
    "- type: rm\n"
    "  flags: 0x00000007\n"
    "  path: /path/to/b\n"
    "- path: /path/to/c\n"
    "  dig: 0:d41d8cd98f00b204e9800998ecf8427e\n"
    "  type: sh\n"
    "- path: /path/to/d\n"
    "  dig: 0:d41d8cd98f00b204e9800998ecf8427e\n"
    "  type: lua\n"
    "- dig: 0:d41d8cd98f00b204e9800998ecf8427e\n"
    "  path: /path/to/e\n"
    "  type: pfirm\n"
    "- path: /path/to/f\n"
    "  dig: 0:d41d8cd98f00b204e9800998ecf8427e\n"
    "  type: rfirm\n"
    "manifest:\n"
    "  millis: 1381285389778\n";
static size_t simple_manifest_swap_size = sizeof(simple_manifest_swap) - 1;

static char simple_manifest_extra[] = \
    "# 0:3fd8fb4b76d6c34b17ee270934f6d5f5\n"
    "---\n"
    "extra_tl_a: data\n"
    "manifest:\n"
    "  extra_a: data\n"
    "  millis: 1381285389778\n"
    "  extra_b:\n"
    "    extra_ba: data\n"
    "    extra_bb: data\n"
    "    extra_bc:\n"
    "      extra_bca: data\n"
    "      extra_bcb: data\n"
    "  extra_c:\n"
    "  - data1\n"
    "  - data2\n"
    "files:\n"
    "- path: /path/to/a\n"
    "  dig: 0:d41d8cd98f00b204e9800998ecf8427e\n"
    "  type: file\n"
    "- path: /path/to/b\n"
    "  type: rm\n"
    "  flags: 0x00000007\n"
    "- path: /path/to/c\n"
    "  dig: 0:d41d8cd98f00b204e9800998ecf8427e\n"
    "  type: sh\n"
    "- path: /path/to/d\n"
    "  dig: 0:d41d8cd98f00b204e9800998ecf8427e\n"
    "  type: lua\n"
    "- path: /path/to/e\n"
    "  dig: 0:d41d8cd98f00b204e9800998ecf8427e\n"
    "  type: pfirm\n"
    "- path: /path/to/f\n"
    "  dig: 0:d41d8cd98f00b204e9800998ecf8427e\n"
    "  type: rfirm\n"
    "  extrakey: data\n"
    "extra_tl_b:\n"
    "- data1\n"
    "- data2\n"
    "- data3\n";
static size_t simple_manifest_extra_size = sizeof(simple_manifest_extra) - 1;

static char simple_manifest_sha1[] = \
    "# 1:3f466b3b49108c72757426f28ed4a4cdce702bb5\n"
    "---\n"
    "manifest:\n"
    "  millis: 1381285389778\n"
    "files:\n"
    "- path: /path/to/a\n"
    "  dig: 0:d41d8cd98f00b204e9800998ecf8427e\n"
    "  type: file\n"
    "- path: /path/to/b\n"
    "  type: rm\n"
    "  flags: 0x00000007\n"
    "- path: /path/to/c\n"
    "  dig: 0:d41d8cd98f00b204e9800998ecf8427e\n"
    "  type: sh\n"
    "- path: /path/to/d\n"
    "  dig: 0:d41d8cd98f00b204e9800998ecf8427e\n"
    "  type: lua\n"
    "- path: /path/to/e\n"
    "  dig: 0:d41d8cd98f00b204e9800998ecf8427e\n"
    "  type: pfirm\n"
    "- path: /path/to/f\n"
    "  dig: 0:d41d8cd98f00b204e9800998ecf8427e\n"
    "  type: rfirm\n";
static size_t simple_manifest_sha1_size = sizeof(simple_manifest_sha1) - 1;

static char bad_1_manifest[] = \
    "# 0:18e549befafbe4319a4d3ef366d5f7bf\n"
    "---\n"
    "manifest:\n"
    "  millis: 1381285389778\n"
    "  - path: /path/to/a\n"
    "    dig: 0:d41d8cd98f00b204e9800998ecf8427e\n";
static size_t bad_1_manifest_size = sizeof(bad_1_manifest) - 1;

static char bad_2_manifest[] = \
    "# 0:d41d8cd98f00b204e9800998ecf8427e\n";
static size_t bad_2_manifest_size = sizeof(bad_2_manifest) - 1;

static char bad_3_manifest[] = \
    "# 0:d5347f99f549f09ab2b4530bcb91b3b8\n"
    "---\n"
    "manifest:\n"
    "  millis: 1381285389778\n"
    "files:\n"
    "- zpath: /path/to/a\n"
    "  zdig: 0:d41d8cd98f00b204e9800998ecf8427e\n";
static size_t bad_3_manifest_size = sizeof(bad_3_manifest) - 1;

static char bad_4_manifest[] = \
    "# 0:b44bb5b0304305c0dff143243093fac1\n"
    "---\n"
    "- path: /path/to/a\n"
    "- path: /path/to/b\n"
    "- path: /path/to/c\n"
    "- path: /path/to/d\n"
    "- path: /path/to/e\n";
static size_t bad_4_manifest_size = sizeof(bad_4_manifest) - 1;

static char bad_5_manifest[] = \
    "# 0:0fc2de5ce0247e69606a750b820a2331\n"
    "---\n"
    "data\n";
static size_t bad_5_manifest_size = sizeof(bad_5_manifest) - 1;

static char bad_6_manifest[] = \
    "# 0:1044de285351bce14e30de941ccbc39b\n"
    "---\n"
    "manifest:\n"
    "  millis: 1381285389778\n"
    "files:\n"
    "- path: /path/to/a\n"
    "  type: file\n"
    "  flags: 0x00000001\n";
static size_t bad_6_manifest_size = sizeof(bad_6_manifest) - 1;

static char bad_7_manifest[] = \
    "# 0:b609cd449fd7d457e2ac0b5a232833a9\n"
    "---\n"
    "manifest:\n"
    "  millis: 1381285389778\n"
    "files:\n"
    "- path: /path/to/a\n"
    "  type: rm\n"
    "  flags: 0x0000000f\n";
static size_t bad_7_manifest_size = sizeof(bad_7_manifest) - 1;

static char bad_8_manifest[] = \
    "# 0:8b75be50f681c2d5dc879f5457ccd685\n"
    "---\n"
    "manifest:\n"
    "  millis: 1381285389778\n"
    "files:\n"
    "- path: /path/to/a\n"
    "  type: sh\n"
    "  flags: 0x00000001\n";
static size_t bad_8_manifest_size = sizeof(bad_8_manifest) - 1;

static char bad_9_manifest[] = \
    "# 0:586a56f148f9306cc47791e11ae2102e\n"
    "---\n"
    "manifest:\n"
    "  millis: 1381285389778\n"
    "files:\n"
    "- path: /path/to/a\n"
    "  type: lua\n"
    "  flags: 0x00000001\n";
static size_t bad_9_manifest_size = sizeof(bad_9_manifest) - 1;

FILE *simple_manifest_file;

START_TEST(manifest_args_test) {
    UpdateManifest manifest;
    UMItem item;
    int index;
    UpdateCode code = UPDATE_OK;

    // These may cause error messages but should not kill the program.
    dump_manifest(NULL);

    ck_assert(open_file_manifest(NULL, &code) == NULL);
    ck_assert_int_ne(code, UPDATE_OK);

    ck_assert(open_mem_manifest(NULL, 0, &code) == NULL);
    ck_assert_int_ne(code, UPDATE_OK);

    code = close_manifest(NULL);
    ck_assert_int_ne(code, UPDATE_OK);

    code = UPDATE_OK;
    ck_assert(manifest_get(NULL, 0, &code) == NULL);
    ck_assert_int_ne(code, UPDATE_OK);

    code = manifest_get_index(NULL, &item, &index);
    ck_assert_int_ne(code, UPDATE_OK);

    code = manifest_get_index(&manifest, NULL, &index);
    ck_assert_int_ne(code, UPDATE_OK);

    code = manifest_get_index(&manifest, &item, NULL);
    ck_assert_int_ne(code, UPDATE_OK);

    code = manifest_delete(NULL, &item);
    ck_assert_int_ne(code, UPDATE_OK);

    code = manifest_delete(&manifest, NULL);
    ck_assert_int_ne(code, UPDATE_OK);

    code = manifest_insert(NULL, &item, 0);
    ck_assert_int_ne(code, UPDATE_OK);

    code = manifest_insert(&manifest, NULL, 0);
    ck_assert_int_ne(code, UPDATE_OK);
}
END_TEST

START_TEST(open_mem_manifest_simple_test) {
    UpdateCode code = UPDATE_ERR;
    UpdateManifest *manifest = open_mem_manifest(simple_manifest,
            simple_manifest_size, &code);
    ck_assert(manifest != NULL);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(manifest->count, SIMPLE_MANIFEST_COUNT);

    code = close_manifest(manifest);
    ck_assert_int_eq(code, UPDATE_OK);

    manifest = NULL;

    // Change digest method.
    simple_manifest[2]++;
    code = UPDATE_ERR;
    manifest = open_mem_manifest(simple_manifest, simple_manifest_size, &code);
    ck_assert(manifest == NULL);
    ck_assert_int_eq(code, UPDATE_DIG_FAIL);
    simple_manifest[2]--;

    // Change digest.
    simple_manifest[4]++;
    code = UPDATE_ERR;
    manifest = open_mem_manifest(simple_manifest, simple_manifest_size, &code);
    ck_assert(manifest == NULL);
    ck_assert_int_eq(code, UPDATE_DIG_FAIL);
    simple_manifest[4]--;

    // Change data.
    simple_manifest[simple_manifest_size - 1]++;
    code = UPDATE_ERR;
    manifest = open_mem_manifest(simple_manifest, simple_manifest_size, &code);
    ck_assert(manifest == NULL);
    ck_assert_int_eq(code, UPDATE_DIG_FAIL);
    simple_manifest[simple_manifest_size - 1]--;
}
END_TEST

START_TEST(open_file_manifest_simple_test) {
    char c;

    UpdateCode code = UPDATE_ERR;
    fseek(simple_manifest_file, 0, SEEK_SET);
    UpdateManifest *manifest = open_file_manifest(simple_manifest_file, &code);
    ck_assert(manifest != NULL);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(manifest->count, SIMPLE_MANIFEST_COUNT);

    code = close_manifest(manifest);
    ck_assert_int_eq(code, UPDATE_OK);

    manifest = NULL;

    // Change digest method.
    fseek(simple_manifest_file, 2, SEEK_SET);
    fread(&c, 1, 1, simple_manifest_file);
    c++;
    fseek(simple_manifest_file, -1, SEEK_CUR);
    fwrite(&c, 1, 1, simple_manifest_file);
    code = UPDATE_ERR;
    fseek(simple_manifest_file, 0, SEEK_SET);
    manifest = open_file_manifest(simple_manifest_file, &code);
    ck_assert(manifest == NULL);
    ck_assert_int_eq(code, UPDATE_DIG_FAIL);
    c--;
    fseek(simple_manifest_file, 2, SEEK_SET);
    fwrite(&c, 1, 1, simple_manifest_file);

    // Change digest.
    fseek(simple_manifest_file, 4, SEEK_SET);
    fread(&c, 1, 1, simple_manifest_file);
    c++;
    fseek(simple_manifest_file, -1, SEEK_CUR);
    fwrite(&c, 1, 1, simple_manifest_file);
    code = UPDATE_ERR;
    fseek(simple_manifest_file, 0, SEEK_SET);
    manifest = open_file_manifest(simple_manifest_file, &code);
    ck_assert(manifest == NULL);
    ck_assert_int_eq(code, UPDATE_DIG_FAIL);
    c--;
    fseek(simple_manifest_file, 4, SEEK_SET);
    fwrite(&c, 1, 1, simple_manifest_file);

    // Change data.
    fseek(simple_manifest_file, -1, SEEK_END);
    fread(&c, 1, 1, simple_manifest_file);
    c++;
    fseek(simple_manifest_file, -1, SEEK_CUR);
    fwrite(&c, 1, 1, simple_manifest_file);
    code = UPDATE_ERR;
    fseek(simple_manifest_file, 0, SEEK_SET);
    manifest = open_file_manifest(simple_manifest_file, &code);
    ck_assert(manifest == NULL);
    ck_assert_int_eq(code, UPDATE_DIG_FAIL);
    c--;
    fseek(simple_manifest_file, -1, SEEK_END);
    fwrite(&c, 1, 1, simple_manifest_file);
}
END_TEST

START_TEST(manifest_get_test) {
    UMItem *item;
    int index;

    UpdateCode code = UPDATE_ERR;
    UpdateManifest *manifest = open_mem_manifest(simple_manifest,
            simple_manifest_size, &code);
    ck_assert(manifest != NULL);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(manifest->count, SIMPLE_MANIFEST_COUNT);

    // Perform get on each item indicated by count.
    for (index = 0; index < manifest->count; index++) {
        item = manifest_get(manifest, index, &code);
        ck_assert(item != NULL);
        ck_assert_int_eq(code, UPDATE_OK);
        ck_assert_int_eq(0, strcmp(simple_manifest_paths[index], item->path));
        ck_assert_int_eq(simple_manifest_flags[index], item->flags);
        ck_assert_int_eq(simple_manifest_types[index], item->type);
    }
    item = manifest_get(manifest, index, &code);

    ck_assert(item == NULL);
    ck_assert_int_eq(code, UPDATE_OOR);

    code = close_manifest(manifest);
    ck_assert_int_eq(code, UPDATE_OK);
}
END_TEST

START_TEST(manifest_get_index_test) {
    UMItem *item1;
    UMItem *item2;
    int index;
    int gindex;
    UpdateCode code = UPDATE_ERR;
    UpdateManifest *manifest1;
    UpdateManifest *manifest2;

    fseek(simple_manifest_file, 0, SEEK_SET);
    manifest1 = open_file_manifest(simple_manifest_file, &code);
    ck_assert(manifest1 != NULL);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(manifest1->count, SIMPLE_MANIFEST_COUNT);

    fseek(simple_manifest_file, 0, SEEK_SET);
    manifest2 = open_file_manifest(simple_manifest_file, &code);
    ck_assert(manifest2 != NULL);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(manifest2->count, SIMPLE_MANIFEST_COUNT);

    for (index = 0; index < manifest1->count; index++) {
        item1 = manifest_get(manifest1, index, &code);
        ck_assert(item1 != NULL);
        ck_assert_int_eq(code, UPDATE_OK);

        item2 = manifest_get(manifest2, index, &code);
        ck_assert(item2 != NULL);
        ck_assert_int_eq(code, UPDATE_OK);

        ck_assert_int_eq(0, strcmp(simple_manifest_paths[index], item1->path));
        ck_assert_int_eq(simple_manifest_flags[index], item1->flags);
        ck_assert_int_eq(simple_manifest_types[index], item1->type);
        ck_assert_int_eq(0, strcmp(simple_manifest_paths[index], item2->path));
        ck_assert_int_eq(simple_manifest_flags[index], item2->flags);
        ck_assert_int_eq(simple_manifest_types[index], item2->type);

        // Check _get with _get_index.
        code = manifest_get_index(manifest1, item1, &gindex);
        ck_assert_int_eq(code, UPDATE_OK);
        ck_assert_int_eq(gindex, index);

        // Check we can't get an index using the wrong manifest.
        code = manifest_get_index(manifest2, item1, &gindex);
        ck_assert_int_ne(code, UPDATE_OK);
        code = manifest_get_index(manifest1, item2, &gindex);
        ck_assert_int_ne(code, UPDATE_OK);
    }

    code = close_manifest(manifest1);
    ck_assert_int_eq(code, UPDATE_OK);
    code = close_manifest(manifest2);
    ck_assert_int_eq(code, UPDATE_OK);
}
END_TEST

START_TEST(manifest_delete_test) {
    UMItem *item;
    UpdateCode code = UPDATE_ERR;
    UpdateManifest *manifest1;
    UpdateManifest *manifest2;
    int index;

    manifest1 = open_mem_manifest(simple_manifest, simple_manifest_size, &code);
    ck_assert(manifest1 != NULL);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(manifest1->count, SIMPLE_MANIFEST_COUNT);

    manifest2 = open_mem_manifest(simple_manifest, simple_manifest_size, &code);
    ck_assert(manifest2 != NULL);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(manifest2->count, SIMPLE_MANIFEST_COUNT);

    // Get an item from manifest1 and try to delete it from manifest2.
    item = manifest_get(manifest1, 1, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    code = manifest_delete(manifest2, item);
    ck_assert_int_ne(code, UPDATE_OK);
    ck_assert_int_eq(manifest2->count, SIMPLE_MANIFEST_COUNT);

    // Then delete it from manifest1 (checks middle delete).
    code = manifest_delete(manifest1, item);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(manifest1->count, SIMPLE_MANIFEST_COUNT - 1);

    item = manifest_get(manifest1, 0, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(0, strcmp(simple_manifest_paths[0], item->path));
    ck_assert_int_eq(simple_manifest_flags[0], item->flags);
    ck_assert_int_eq(simple_manifest_types[0], item->type);
    item = manifest_get(manifest1, 1, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(0, strcmp(simple_manifest_paths[2], item->path));
    ck_assert_int_eq(simple_manifest_flags[2], item->flags);
    ck_assert_int_eq(simple_manifest_types[2], item->type);

    // Head delete with remainder.
    item = manifest_get(manifest1, 0, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    code = manifest_delete(manifest1, item);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(manifest1->count, SIMPLE_MANIFEST_COUNT - 2);
    item = manifest_get(manifest1, 0, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(0, strcmp(simple_manifest_paths[2], item->path));
    ck_assert_int_eq(simple_manifest_flags[2], item->flags);
    ck_assert_int_eq(simple_manifest_types[2], item->type);

    // Head delete no remainder.
    for (index = 0; index < SIMPLE_MANIFEST_COUNT - 2; index++) {
        item = manifest_get(manifest1, 0, &code);
        ck_assert_int_eq(code, UPDATE_OK);
        code = manifest_delete(manifest1, item);
    }
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(manifest1->count, 0);

    // Delete item from manifest2 on now empty manifest1.
    item = manifest_get(manifest2, 4, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    code = manifest_delete(manifest1, item);
    ck_assert_int_ne(code, UPDATE_OK);
    ck_assert_int_eq(manifest1->count, 0);

    // Tail delete.
    code = manifest_delete(manifest2, item);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(manifest2->count, SIMPLE_MANIFEST_COUNT - 1);

    code = close_manifest(manifest1);
    ck_assert_int_eq(code, UPDATE_OK);
    code = close_manifest(manifest2);
    ck_assert_int_eq(code, UPDATE_OK);
}
END_TEST

START_TEST(manifest_item_new_free_test) {
    UMItem *item;

    UpdateCode code = UPDATE_ERR;
    UpdateManifest *manifest = open_mem_manifest(simple_manifest,
            simple_manifest_size, &code);
    ck_assert(manifest != NULL);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(manifest->count, SIMPLE_MANIFEST_COUNT);

    item = manifest_get(manifest, 1, &code);
    ck_assert_int_eq(code, UPDATE_OK);

    // Make sure we can't free an item associated with a manifest.
    code = manifest_item_free(item);
    ck_assert_int_ne(code, UPDATE_OK);

    // Create and free an item.
    item = manifest_item_new();
    ck_assert(item != NULL);
    code = manifest_item_free(item);
    ck_assert_int_eq(code, UPDATE_OK);

    code = close_manifest(manifest);
    ck_assert_int_eq(code, UPDATE_OK);
}
END_TEST

START_TEST(manifest_insert_test) {
    char test_path[] = "/path/to/a";
    char test_digest[] = "d41d8cd98f00b204e9800998ecf8427e";
    char *last_char = &test_path[sizeof(test_path) - 2];
    UMItem *item;
    UMItem *gitem;
    int index;

    UpdateCode code = UPDATE_ERR;
    UpdateManifest *manifest = open_mem_manifest(empty_manifest,
            empty_manifest_size, &code);
    ck_assert(manifest != NULL);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(manifest->count, 0);

    // Insert to empty with index 0.
    item = manifest_item_new();
    item->path = bstrdup(test_path);
    item->digest = bstrdup(test_digest);
    code = manifest_insert(manifest, item, 0);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(manifest->count, 1);
    gitem = manifest_get(manifest, 0, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(gitem == item);
    code = manifest_delete(manifest, item);
    ck_assert_int_eq(manifest->count, 0);
    ck_assert_int_eq(code, UPDATE_OK);

    // Insert to empty with INSERT_FIRST.
    item = manifest_item_new();
    item->path = bstrdup(test_path);
    item->digest = bstrdup(test_digest);
    code = manifest_insert(manifest, item, INSERT_FIRST);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(manifest->count, 1);
    gitem = manifest_get(manifest, 0, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(gitem == item);
    code = manifest_delete(manifest, item);
    ck_assert_int_eq(manifest->count, 0);
    ck_assert_int_eq(code, UPDATE_OK);

    // Insert to empty with INSERT_LAST.
    item = manifest_item_new();
    item->path = bstrdup(test_path);
    item->digest = bstrdup(test_digest);
    code = manifest_insert(manifest, item, INSERT_LAST);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(manifest->count, 1);
    gitem = manifest_get(manifest, 0, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(gitem == item);
    code = manifest_delete(manifest, item);
    ck_assert_int_eq(manifest->count, 0);
    ck_assert_int_eq(code, UPDATE_OK);

    // Add some data.
    for (index = 0; index < 5; index++) {
        item = manifest_item_new();
        item->path = bstrdup(test_path);
        item->digest = bstrdup(test_digest);
        code = manifest_insert(manifest, item, INSERT_LAST);
        ck_assert_int_eq(code, UPDATE_OK);
    }

    // Insert first using INSERT_FIRST.
    (*last_char)++;
    item = manifest_item_new();
    item->path = bstrdup(test_path);
    item->digest = bstrdup(test_digest);
    code = manifest_insert(manifest, item, INSERT_FIRST);
    ck_assert_int_eq(code, UPDATE_OK);
    gitem = manifest_get(manifest, 0, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(gitem == item);
    ck_assert_int_eq(0, strcmp(gitem->path, test_path));

    // Insert right after first.
    (*last_char)++;
    item = manifest_item_new();
    item->path = bstrdup(test_path);
    item->digest = bstrdup(test_digest);
    code = manifest_insert(manifest, item, 0);
    ck_assert_int_eq(code, UPDATE_OK);
    gitem = manifest_get(manifest, 1, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(gitem == item);
    ck_assert_int_eq(0, strcmp(gitem->path, test_path));

    // Insert last using INSERT_LAST (this was used above but not checked
    // that it actually went to the right place).
    (*last_char)++;
    item = manifest_item_new();
    item->path = bstrdup(test_path);
    item->digest = bstrdup(test_digest);
    code = manifest_insert(manifest, item, INSERT_LAST);
    ck_assert_int_eq(code, UPDATE_OK);
    gitem = manifest_get(manifest, manifest->count - 1, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(gitem == item);
    ck_assert_int_eq(0, strcmp(gitem->path, test_path));

    // Insert last using last index.
    (*last_char)++;
    item = manifest_item_new();
    item->path = bstrdup(test_path);
    item->digest = bstrdup(test_digest);
    code = manifest_insert(manifest, item, manifest->count - 1);
    ck_assert_int_eq(code, UPDATE_OK);
    gitem = manifest_get(manifest, manifest->count - 1, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(gitem == item);
    ck_assert_int_eq(0, strcmp(gitem->path, test_path));

    // Insert right before last index.
    (*last_char)++;
    item = manifest_item_new();
    item->path = bstrdup(test_path);
    item->digest = bstrdup(test_digest);
    code = manifest_insert(manifest, item, manifest->count - 2);
    ck_assert_int_eq(code, UPDATE_OK);
    gitem = manifest_get(manifest, manifest->count - 2, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(gitem == item);
    ck_assert_int_eq(0, strcmp(gitem->path, test_path));

    // Insert to the middle, new item should be index + 1.
    index = manifest->count / 2;
    (*last_char)++;
    item = manifest_item_new();
    item->path = bstrdup(test_path);
    item->digest = bstrdup(test_digest);
    code = manifest_insert(manifest, item, index);
    ck_assert_int_eq(code, UPDATE_OK);
    gitem = manifest_get(manifest, index + 1, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(gitem == item);
    ck_assert_int_eq(0, strcmp(gitem->path, test_path));

    // Insert last using bad negative value.
    (*last_char)++;
    item = manifest_item_new();
    item->path = bstrdup(test_path);
    item->digest = bstrdup(test_digest);
    code = manifest_insert(manifest, item, -0xbad);
    ck_assert_int_eq(code, UPDATE_OK);
    gitem = manifest_get(manifest, manifest->count - 1, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(gitem == item);
    ck_assert_int_eq(0, strcmp(gitem->path, test_path));

    // Insert last using bad positive value.
    (*last_char)++;
    item = manifest_item_new();
    item->path = bstrdup(test_path);
    item->digest = bstrdup(test_digest);
    code = manifest_insert(manifest, item, 0xbad);
    ck_assert_int_eq(code, UPDATE_OK);
    gitem = manifest_get(manifest, manifest->count - 1, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(gitem == item);
    ck_assert_int_eq(0, strcmp(gitem->path, test_path));

    code = close_manifest(manifest);
    ck_assert_int_eq(code, UPDATE_OK);
}
END_TEST

START_TEST(open_alt_manifest_test) {
    UMItem *item;
    UMItem *swap_item;
    UMItem *extra_item;
    UMItem *sha1_item;
    int index;
    UpdateCode code = UPDATE_ERR;
    UpdateManifest *manifest;
    UpdateManifest *swap_manifest;
    UpdateManifest *extra_manifest;
    UpdateManifest *sha1_manifest;

    manifest = open_mem_manifest(simple_manifest, simple_manifest_size, &code);
    ck_assert(manifest != NULL);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(manifest->count, SIMPLE_MANIFEST_COUNT);

    swap_manifest = open_mem_manifest(simple_manifest_swap,
            simple_manifest_swap_size, &code);
    ck_assert(swap_manifest != NULL);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(swap_manifest->count, SIMPLE_MANIFEST_COUNT);

    extra_manifest = open_mem_manifest(simple_manifest_extra,
            simple_manifest_extra_size, &code);
    ck_assert(extra_manifest != NULL);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(extra_manifest->count, SIMPLE_MANIFEST_COUNT);

    sha1_manifest = open_mem_manifest(simple_manifest_sha1,
            simple_manifest_sha1_size, &code);
    ck_assert(sha1_manifest != NULL);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(sha1_manifest->count, SIMPLE_MANIFEST_COUNT);

    for (index = 0; index < manifest->count; index++) {
        item = manifest_get(manifest, index, &code);
        ck_assert_int_eq(code, UPDATE_OK);
        swap_item = manifest_get(swap_manifest, index, &code);
        ck_assert_int_eq(code, UPDATE_OK);
        extra_item = manifest_get(extra_manifest, index, &code);
        ck_assert_int_eq(code, UPDATE_OK);
        sha1_item = manifest_get(sha1_manifest, index, &code);
        ck_assert_int_eq(code, UPDATE_OK);

        ck_assert_int_eq(0, strcmp(item->path, swap_item->path));
        ck_assert_int_eq(item->flags, swap_item->flags);
        ck_assert_int_eq(item->method, swap_item->method);
        // Compare digests if type is not REMOVE.  If type is
        // REMOVE make sure it is NULL.
        if (item->type == UMITEM_REMOVE) {
            ck_assert_int_eq(0, item->digest);
            ck_assert_int_eq(0, swap_item->digest);
        } else {
            ck_assert_int_eq(0, strcmp(item->digest, swap_item->digest));
        }

        ck_assert_int_eq(0, strcmp(item->path, extra_item->path));
        ck_assert_int_eq(item->flags, extra_item->flags);
        ck_assert_int_eq(item->method, extra_item->method);
        // Compare digests if type is not REMOVE.  If type is
        // REMOVE make sure it is NULL.
        if (item->type == UMITEM_REMOVE) {
            ck_assert_int_eq(0, item->digest);
            ck_assert_int_eq(0, extra_item->digest);
        } else {
            ck_assert_int_eq(0, strcmp(item->digest, extra_item->digest));
        }

        ck_assert_int_eq(0, strcmp(item->path, sha1_item->path));
        ck_assert_int_eq(item->flags, sha1_item->flags);
        ck_assert_int_eq(item->method, sha1_item->method);
        // Compare digests if type is not REMOVE.  If type is
        // REMOVE make sure it is NULL.
        if (item->type == UMITEM_REMOVE) {
            ck_assert_int_eq(0, item->digest);
            ck_assert_int_eq(0, sha1_item->digest);
        } else {
            ck_assert_int_eq(0, strcmp(item->digest, sha1_item->digest));
        }
    }

    code = close_manifest(manifest);
    ck_assert_int_eq(code, UPDATE_OK);

    code = close_manifest(swap_manifest);
    ck_assert_int_eq(code, UPDATE_OK);

    code = close_manifest(extra_manifest);
    ck_assert_int_eq(code, UPDATE_OK);

    code = close_manifest(sha1_manifest);
    ck_assert_int_eq(code, UPDATE_OK);
}
END_TEST

START_TEST(open_bad_manifest_test) {
    UpdateCode code = UPDATE_OK;
    UpdateManifest *manifest;

    manifest = open_mem_manifest(bad_1_manifest, bad_1_manifest_size, &code);
    ck_assert(manifest != NULL);
    ck_assert_int_ne(code, UPDATE_OK);
    code = close_manifest(manifest);
    ck_assert_int_eq(code, UPDATE_OK);

    manifest = open_mem_manifest(bad_2_manifest, bad_2_manifest_size, &code);
    ck_assert(manifest != NULL);
    ck_assert_int_ne(code, UPDATE_OK);
    code = close_manifest(manifest);
    ck_assert_int_eq(code, UPDATE_OK);

    manifest = open_mem_manifest(bad_3_manifest, bad_3_manifest_size, &code);
    ck_assert(manifest != NULL);
    ck_assert_int_ne(code, UPDATE_OK);
    code = close_manifest(manifest);
    ck_assert_int_eq(code, UPDATE_OK);

    manifest = open_mem_manifest(bad_4_manifest, bad_4_manifest_size, &code);
    ck_assert(manifest != NULL);
    ck_assert_int_ne(code, UPDATE_OK);
    code = close_manifest(manifest);
    ck_assert_int_eq(code, UPDATE_OK);

    manifest = open_mem_manifest(bad_5_manifest, bad_5_manifest_size, &code);
    ck_assert(manifest != NULL);
    ck_assert_int_ne(code, UPDATE_OK);
    code = close_manifest(manifest);
    ck_assert_int_eq(code, UPDATE_OK);

    manifest = open_mem_manifest(bad_6_manifest, bad_6_manifest_size, &code);
    ck_assert(manifest != NULL);
    ck_assert_int_ne(code, UPDATE_OK);
    code = close_manifest(manifest);
    ck_assert_int_eq(code, UPDATE_OK);

    manifest = open_mem_manifest(bad_7_manifest, bad_7_manifest_size, &code);
    ck_assert(manifest != NULL);
    ck_assert_int_ne(code, UPDATE_OK);
    code = close_manifest(manifest);
    ck_assert_int_eq(code, UPDATE_OK);

    manifest = open_mem_manifest(bad_8_manifest, bad_8_manifest_size, &code);
    ck_assert(manifest != NULL);
    ck_assert_int_ne(code, UPDATE_OK);
    code = close_manifest(manifest);
    ck_assert_int_eq(code, UPDATE_OK);

    manifest = open_mem_manifest(bad_9_manifest, bad_9_manifest_size, &code);
    ck_assert(manifest != NULL);
    ck_assert_int_ne(code, UPDATE_OK);
    code = close_manifest(manifest);
    ck_assert_int_eq(code, UPDATE_OK);
}
END_TEST

START_TEST(save_manifest_test) {
    UpdateCode code = UPDATE_ERR;
    UpdateManifest *e_manifest;  // empty
    UpdateManifest *s_manifest;  // simple
    UpdateManifest *t1_manifest;  // temp
    UpdateManifest *t2_manifest;  // temp
    UMItem *item;
    FILE *stream1;
    FILE *stream2;
    FILE *stream3;
    char *p;
    char wtest[] = "check";
    char rtest[sizeof(wtest)];
    int fd_flags;
    int fd;

    e_manifest = open_mem_manifest(empty_manifest, empty_manifest_size, &code);
    ck_assert(e_manifest != NULL);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(e_manifest->count, 0);

    s_manifest = open_mem_manifest(simple_manifest, simple_manifest_size,
            &code);
    ck_assert(s_manifest != NULL);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(s_manifest->count, SIMPLE_MANIFEST_COUNT);

    // Save empty manifest.
    stream1 = tmpfopen("w");
    ck_assert(stream1 != NULL);
    // stream1 is mode:w
    stream2 = save_manifest(e_manifest, stream1, DIGEST_MD5, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(stream2 != NULL);
    ck_assert(stream1 == stream2);
    fd = fileno(stream2);
    ck_assert_int_ne(fd, -1);
    fd_flags = fcntl(fd, F_GETFD);
    ck_assert((fd_flags & O_ACCMODE) == O_RDONLY);
    t1_manifest = open_file_manifest(stream2, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(t1_manifest != NULL);
    ck_assert(e_manifest->last_modified == t1_manifest->last_modified);
    ck_assert(e_manifest->count == t1_manifest->count);

    // Save single item manifest with NULL stream when pm->isfile == true.
    item = manifest_item_new();
    item->path = bstrdup("/path/to/a");
    item->method = DIGEST_MD5;
    item->digest = bstrdup("d41d8cd98f00b204e9800998ecf8427e");
    item->type = UMITEM_FILE;
    code = manifest_insert(t1_manifest, item, INSERT_LAST);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(t1_manifest->count, 1);
    // stream1 is mode:r
    stream2 = save_manifest(t1_manifest, NULL, 0, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(stream2 != NULL);
    ck_assert(stream1 == stream2);
    fd = fileno(stream2);
    ck_assert_int_ne(fd, -1);
    fd_flags = fcntl(fd, F_GETFD);
    ck_assert((fd_flags & O_ACCMODE) == O_RDONLY);
    t2_manifest = open_file_manifest(stream2, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(t2_manifest != NULL);
    ck_assert(t1_manifest->last_modified == t2_manifest->last_modified);
    ck_assert(t1_manifest->count == t2_manifest->count);

    // Cleanup and close.
    code = close_manifest(t1_manifest);
    ck_assert_int_eq(code, UPDATE_OK);
    code = close_manifest(t2_manifest);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(tmpfunclose(stream2) == 0);

    // Save simple manifest as t1_manifest with one item deleted, read
    // back into t2_manifest and compare.
    stream1 = tmpfopen("w");
    ck_assert(stream1 != NULL);
    stream2 = save_manifest(s_manifest, stream1, DIGEST_MD5, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(stream2 != NULL);
    fd = fileno(stream2);
    ck_assert_int_ne(fd, -1);
    fd_flags = fcntl(fd, F_GETFD);
    ck_assert((fd_flags & O_ACCMODE) == O_RDONLY);
    t1_manifest = open_file_manifest(stream2, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(t1_manifest != NULL);
    ck_assert(s_manifest->last_modified == t1_manifest->last_modified);
    ck_assert(s_manifest->count == t1_manifest->count);

    // Save modified simple manifest (also saves using stream argument when
    // pm->isfile == true).
    item = manifest_get(t1_manifest, 0, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(item != NULL);
    code = manifest_delete(t1_manifest, item);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert_int_eq(t1_manifest->count, SIMPLE_MANIFEST_COUNT - 1);
    stream2 = tmpfopen("w");
    ck_assert(stream2 != NULL);
    // stream2 is mode:w
    stream3 = save_manifest(t1_manifest, stream2, DIGEST_SHA1, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(stream3 != NULL);
    ck_assert(stream2 == stream3);
    fd = fileno(stream3);
    ck_assert_int_ne(fd, -1);
    fd_flags = fcntl(fd, F_GETFD);
    ck_assert((fd_flags & O_ACCMODE) == O_RDONLY);
    t2_manifest = open_file_manifest(stream3, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(t2_manifest != NULL);
    ck_assert(t1_manifest->last_modified == t2_manifest->last_modified);
    ck_assert(t1_manifest->count == t2_manifest->count);

    // Cleanup and close.
    code = close_manifest(t1_manifest);
    ck_assert_int_eq(code, UPDATE_OK);
    code = close_manifest(t2_manifest);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(tmpfunclose(stream1) == 0);
    ck_assert(tmpfunclose(stream2) == 0);

    // Check NULL manifest.
    stream1 = tmpfopen("w");
    fwrite(&wtest, 1, sizeof(wtest), stream1);
    fflush(stream1);
    ck_assert(stream1 != NULL);
    stream2 = save_manifest(NULL, stream1, DIGEST_MD5, &code);
    ck_assert_int_ne(code, UPDATE_OK);
    ck_assert(stream2 == NULL);

    // Check NULL stream when pm->isfile == false.
    stream2 = save_manifest(s_manifest, NULL, DIGEST_MD5, &code);
    ck_assert_int_ne(code, UPDATE_OK);
    ck_assert(stream2 == NULL);

    // Check bad method.
    stream2 = save_manifest(s_manifest, stream1, DIGEST_INVALID, &code);
    ck_assert_int_ne(code, UPDATE_OK);
    ck_assert(stream2 == stream1);

    // Check bad item.
    item = manifest_get(s_manifest, 0, &code);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(item != NULL);
    p = item->path;
    item->path = NULL;
    stream2 = save_manifest(s_manifest, stream1, DIGEST_MD5, &code);
    ck_assert_int_ne(code, UPDATE_OK);
    ck_assert(stream2 == stream1);
    item->path = p;

    // Check bad count.
    s_manifest->count++;
    stream2 = save_manifest(s_manifest, stream1, DIGEST_MD5, &code);
    ck_assert_int_ne(code, UPDATE_OK);
    ck_assert(stream2 == stream1);

    // Check that stream1 was not altered.
    p = tmpfpath(stream1);
    stream2 = freopen(p, "r", stream1);
    ck_assert(stream1 == stream2);
    memset(rtest, 0, sizeof(wtest));
    fread(rtest, 1, sizeof(wtest), stream1);
    ck_assert_int_eq(0, strcmp(rtest, wtest));
    bfree(p);

    // Cleanup and close.
    code = close_manifest(e_manifest);
    ck_assert_int_eq(code, UPDATE_OK);
    code = close_manifest(s_manifest);
    ck_assert_int_eq(code, UPDATE_OK);
    ck_assert(tmpfunclose(stream1) == 0);
}
END_TEST


Suite *manifest_suite(void) {
    simple_manifest_file = tmpfopen("w+");
    assert(simple_manifest_file);
    fwrite(simple_manifest, 1, simple_manifest_size, simple_manifest_file);
    fseek(simple_manifest_file, 0, SEEK_SET);

    Suite *s = suite_create("manifest");

    TCase *tc_core = tcase_create("core");
    tcase_add_test(tc_core, manifest_args_test);
    tcase_add_test(tc_core, open_file_manifest_simple_test);
    tcase_add_test(tc_core, open_mem_manifest_simple_test);
    tcase_add_test(tc_core, manifest_get_test);
    tcase_add_test(tc_core, manifest_get_index_test);
    tcase_add_test(tc_core, manifest_delete_test);
    tcase_add_test(tc_core, manifest_item_new_free_test);
    tcase_add_test(tc_core, manifest_insert_test);
    tcase_add_test(tc_core, open_alt_manifest_test);
    tcase_add_test(tc_core, open_bad_manifest_test);
    tcase_add_test(tc_core, save_manifest_test);
    suite_add_tcase(s, tc_core);

    return s;
}

void cleanup_manifest_suite(void) {
    tmpfunclose(simple_manifest_file);
}
