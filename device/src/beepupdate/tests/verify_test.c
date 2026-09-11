#include <assert.h>
#include <check.h>
#include <stdio.h>
#include <stdlib.h>

#include "beepupdate.h"

static char vector[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
static const char *embedded_fmt = "%s%c:%s\n%.*s";

static FILE *vector_file;
static FILE *bad_vector_file;

// Answers are for
// 0 = ""
// 1 = "a"
// 2 = "abc"
// 3 = vector

static char md5_buf_ka_0[] = "\xd4\x1d\x8c\xd9\x8f\x00\xb2\x04\xe9\x80\x09\x98\xec\xf8\x42\x7e";
static char md5_buf_ka_1[] = "\x0c\xc1\x75\xb9\xc0\xf1\xb6\xa8\x31\xc3\x99\xe2\x69\x77\x26\x61";
static char md5_buf_ka_2[] = "\x90\x01\x50\x98\x3c\xd2\x4f\xb0\xd6\x96\x3f\x7d\x28\xe1\x7f\x72";
static char md5_buf_ka_3[] = "\xd1\x74\xab\x98\xd2\x77\xd9\xf5\xa5\x61\x1c\x2c\x9f\x41\x9d\x9f";

static char md5_hex_ka_0[] = "d41d8cd98f00b204e9800998ecf8427e";
static char md5_hex_ka_1[] = "0cc175b9c0f1b6a831c399e269772661";
static char md5_hex_ka_2[] = "900150983cd24fb0d6963f7d28e17f72";
static char md5_hex_ka_3[] = "d174ab98d277d9f5a5611c2c9f419d9f";

static char sha1_buf_ka_0[] = "\xda\x39\xa3\xee\x5e\x6b\x4b\x0d\x32\x55\xbf\xef\x95\x60\x18\x90\xaf\xd8\x07\x09";
static char sha1_buf_ka_1[] = "\x86\xf7\xe4\x37\xfa\xa5\xa7\xfc\xe1\x5d\x1d\xdc\xb9\xea\xea\xea\x37\x76\x67\xb8";
static char sha1_buf_ka_2[] = "\xa9\x99\x3e\x36\x47\x06\x81\x6a\xba\x3e\x25\x71\x78\x50\xc2\x6c\x9c\xd0\xd8\x9d";
static char sha1_buf_ka_3[] = "\x76\x1c\x45\x7b\xf7\x3b\x14\xd2\x7e\x9e\x92\x65\xc4\x6f\x4b\x4d\xda\x11\xf9\x40";

static char sha1_hex_ka_0[] = "da39a3ee5e6b4b0d3255bfef95601890afd80709";
static char sha1_hex_ka_1[] = "86f7e437faa5a7fce15d1ddcb9eaeaea377667b8";
static char sha1_hex_ka_2[] = "a9993e364706816aba3e25717850c26c9cd0d89d";
static char sha1_hex_ka_3[] = "761c457bf73b14d27e9e9265c46f4b4dda11f940";

static char sha256_buf_ka_0[] = "\xe3\xb0\xc4\x42\x98\xfc\x1c\x14\x9a\xfb\xf4\xc8\x99\x6f\xb9\x24\x27\xae\x41\xe4\x64\x9b\x93\x4c\xa4\x95\x99\x1b\x78\x52\xb8\x55";
static char sha256_buf_ka_1[] = "\xca\x97\x81\x12\xca\x1b\xbd\xca\xfa\xc2\x31\xb3\x9a\x23\xdc\x4d\xa7\x86\xef\xf8\x14\x7c\x4e\x72\xb9\x80\x77\x85\xaf\xee\x48\xbb";
static char sha256_buf_ka_2[] = "\xba\x78\x16\xbf\x8f\x01\xcf\xea\x41\x41\x40\xde\x5d\xae\x22\x23\xb0\x03\x61\xa3\x96\x17\x7a\x9c\xb4\x10\xff\x61\xf2\x00\x15\xad";
static char sha256_buf_ka_3[] = "\xdb\x4b\xfc\xbd\x4d\xa0\xcd\x85\xa6\x0c\x3c\x37\xd3\xfb\xd8\x80\x5c\x77\xf1\x5f\xc6\xb1\xfd\xfe\x61\x4e\xe0\xa7\xc8\xfd\xb4\xc0";

static char sha256_hex_ka_0[] = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
static char sha256_hex_ka_1[] = "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb";
static char sha256_hex_ka_2[] = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
static char sha256_hex_ka_3[] = "db4bfcbd4da0cd85a60c3c37d3fbd8805c77f15fc6b1fdfe614ee0a7c8fdb4c0";

static char sha512_buf_ka_0[] = "\xcf\x83\xe1\x35\x7e\xef\xb8\xbd\xf1\x54\x28\x50\xd6\x6d\x80\x07\xd6\x20\xe4\x05\x0b\x57\x15\xdc\x83\xf4\xa9\x21\xd3\x6c\xe9\xce\x47\xd0\xd1\x3c\x5d\x85\xf2\xb0\xff\x83\x18\xd2\x87\x7e\xec\x2f\x63\xb9\x31\xbd\x47\x41\x7a\x81\xa5\x38\x32\x7a\xf9\x27\xda\x3e";
static char sha512_buf_ka_1[] = "\x1f\x40\xfc\x92\xda\x24\x16\x94\x75\x09\x79\xee\x6c\xf5\x82\xf2\xd5\xd7\xd2\x8e\x18\x33\x5d\xe0\x5a\xbc\x54\xd0\x56\x0e\x0f\x53\x02\x86\x0c\x65\x2b\xf0\x8d\x56\x02\x52\xaa\x5e\x74\x21\x05\x46\xf3\x69\xfb\xbb\xce\x8c\x12\xcf\xc7\x95\x7b\x26\x52\xfe\x9a\x75";
static char sha512_buf_ka_2[] = "\xdd\xaf\x35\xa1\x93\x61\x7a\xba\xcc\x41\x73\x49\xae\x20\x41\x31\x12\xe6\xfa\x4e\x89\xa9\x7e\xa2\x0a\x9e\xee\xe6\x4b\x55\xd3\x9a\x21\x92\x99\x2a\x27\x4f\xc1\xa8\x36\xba\x3c\x23\xa3\xfe\xeb\xbd\x45\x4d\x44\x23\x64\x3c\xe8\x0e\x2a\x9a\xc9\x4f\xa5\x4c\xa4\x9f";
static char sha512_buf_ka_3[] = "\x1e\x07\xbe\x23\xc2\x6a\x86\xea\x37\xea\x81\x0c\x8e\xc7\x80\x93\x52\x51\x5a\x97\x0e\x92\x53\xc2\x6f\x53\x6c\xfc\x7a\x99\x96\xc4\x5c\x83\x70\x58\x3e\x0a\x78\xfa\x4a\x90\x04\x1d\x71\xa4\xce\xab\x74\x23\xf1\x9c\x71\xb9\xd5\xa3\xe0\x12\x49\xf0\xbe\xbd\x58\x94";

static char sha512_hex_ka_0[] = "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e";
static char sha512_hex_ka_1[] = "1f40fc92da241694750979ee6cf582f2d5d7d28e18335de05abc54d0560e0f5302860c652bf08d560252aa5e74210546f369fbbbce8c12cfc7957b2652fe9a75";
static char sha512_hex_ka_2[] = "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f";
static char sha512_hex_ka_3[] = "1e07be23c26a86ea37ea810c8ec7809352515a970e9253c26f536cfc7a9996c45c8370583e0a78fa4a90041d71a4ceab7423f19c71b9d5a3e01249f0bebd5894";


static void *create_embedded_buf(const void *ka, size_t ka_size,
        const void *data, size_t data_size, DigestMethod method,
        size_t *size) {
    size_t rsize = ka_size + data_size + 2;
    char *buf = bmalloc(rsize);
    ck_assert(buf != NULL);
    buf[0] = (char)method;
    buf[1] = ':';
    memcpy(buf + 2, ka, ka_size);
    memcpy(buf + 2 + ka_size, data, data_size);
    *size = rsize;
    return buf;
}

START_TEST(create_file_buf_hash_test) {
    uint8_t *buf = NULL;
    size_t size = 0;

    ck_assert(create_file_buf_hash(NULL, 0, DIGEST_MD5, &size) == NULL);
    ck_assert(create_file_buf_hash(vector_file, 0, DIGEST_INVALID, &size) == NULL);

    /// MD5
    fseek(vector_file, 0, SEEK_SET);
    buf = create_file_buf_hash(vector_file, 0, DIGEST_MD5, &size);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, md5_buf_ka_0, size));
    ck_assert_int_eq(16, size);
    bfree(buf);

    fseek(vector_file, 0, SEEK_END);
    buf = create_file_buf_hash(vector_file, 1, DIGEST_MD5, NULL);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, md5_buf_ka_0, size));
    bfree(buf);

    fseek(vector_file, 26, SEEK_SET);
    buf = create_file_buf_hash(vector_file, 1, DIGEST_MD5, NULL);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, md5_buf_ka_1, size));
    bfree(buf);

    buf = create_file_buf_hash(vector_file, 3, DIGEST_MD5, NULL);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, md5_buf_ka_2, size));
    bfree(buf);

    fseek(vector_file, 0, SEEK_SET);
    buf = create_file_buf_hash(vector_file, FILE_END, DIGEST_MD5, NULL);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, md5_buf_ka_3, size));
    bfree(buf);

    // SHA1
    fseek(vector_file, 0, SEEK_SET);
    buf = create_file_buf_hash(vector_file, 0, DIGEST_SHA1, &size);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, sha1_buf_ka_0, size));
    ck_assert_int_eq(20, size);
    bfree(buf);

    fseek(vector_file, 0, SEEK_END);
    buf = create_file_buf_hash(vector_file, 1, DIGEST_SHA1, NULL);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, sha1_buf_ka_0, size));
    bfree(buf);

    fseek(vector_file, 26, SEEK_SET);
    buf = create_file_buf_hash(vector_file, 1, DIGEST_SHA1, NULL);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, sha1_buf_ka_1, size));
    bfree(buf);

    buf = create_file_buf_hash(vector_file, 3, DIGEST_SHA1, NULL);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, sha1_buf_ka_2, size));
    bfree(buf);

    fseek(vector_file, 0, SEEK_SET);
    buf = create_file_buf_hash(vector_file, FILE_END, DIGEST_SHA1, NULL);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, sha1_buf_ka_3, size));
    bfree(buf);

    // SHA256
    fseek(vector_file, 0, SEEK_SET);
    buf = create_file_buf_hash(vector_file, 0, DIGEST_SHA256, &size);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, sha256_buf_ka_0, size));
    ck_assert_int_eq(32, size);
    bfree(buf);

    fseek(vector_file, 0, SEEK_END);
    buf = create_file_buf_hash(vector_file, 1, DIGEST_SHA256, NULL);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, sha256_buf_ka_0, size));
    bfree(buf);

    fseek(vector_file, 26, SEEK_SET);
    buf = create_file_buf_hash(vector_file, 1, DIGEST_SHA256, NULL);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, sha256_buf_ka_1, size));
    bfree(buf);

    buf = create_file_buf_hash(vector_file, 3, DIGEST_SHA256, NULL);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, sha256_buf_ka_2, size));
    bfree(buf);

    fseek(vector_file, 0, SEEK_SET);
    buf = create_file_buf_hash(vector_file, FILE_END, DIGEST_SHA256, NULL);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, sha256_buf_ka_3, size));
    bfree(buf);

    // SHA512
    fseek(vector_file, 0, SEEK_SET);
    buf = create_file_buf_hash(vector_file, 0, DIGEST_SHA512, &size);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, sha512_buf_ka_0, size));
    ck_assert_int_eq(64, size);
    bfree(buf);

    fseek(vector_file, 0, SEEK_END);
    buf = create_file_buf_hash(vector_file, 1, DIGEST_SHA512, NULL);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, sha512_buf_ka_0, size));
    bfree(buf);

    fseek(vector_file, 26, SEEK_SET);
    buf = create_file_buf_hash(vector_file, 1, DIGEST_SHA512, NULL);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, sha512_buf_ka_1, size));
    bfree(buf);

    buf = create_file_buf_hash(vector_file, 3, DIGEST_SHA512, NULL);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, sha512_buf_ka_2, size));
    bfree(buf);

    fseek(vector_file, 0, SEEK_SET);
    buf = create_file_buf_hash(vector_file, FILE_END, DIGEST_SHA512, NULL);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, sha512_buf_ka_3, size));
    bfree(buf);
}
END_TEST

START_TEST(create_mem_buf_hash_test) {
    uint8_t *buf = NULL;
    size_t size = 0;

    ck_assert(create_mem_buf_hash(NULL, 0, DIGEST_MD5, &size) == NULL);
    ck_assert(create_mem_buf_hash(vector, 0, DIGEST_INVALID, &size) == NULL);

    /// MD5
    buf = create_mem_buf_hash(vector, 0, DIGEST_MD5, &size);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, md5_buf_ka_0, size));
    ck_assert_int_eq(16, size);
    bfree(buf);

    buf = create_mem_buf_hash(vector + 26, 1, DIGEST_MD5, NULL);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, md5_buf_ka_1, size));
    bfree(buf);

    buf = create_mem_buf_hash(vector + 26, 3, DIGEST_MD5, NULL);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, md5_buf_ka_2, size));
    bfree(buf);

    buf = create_mem_buf_hash(vector, strlen(vector), DIGEST_MD5, NULL);
    ck_assert(buf != NULL);
    ck_assert_int_eq(0, memcmp(buf, md5_buf_ka_3, size));
    bfree(buf);
}
END_TEST

START_TEST(create_file_hex_hash_test) {
    char *hex = NULL;
    size_t size = 0;

    ck_assert(create_file_hex_hash(NULL, 0, DIGEST_MD5, &size) == NULL);
    ck_assert(create_file_hex_hash(vector_file, 0, DIGEST_INVALID, &size) == NULL);

    /// MD5
    fseek(vector_file, 0, SEEK_SET);
    hex = create_file_hex_hash(vector_file, 0, DIGEST_MD5, &size);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, md5_hex_ka_0, size));
    ck_assert_int_eq(32, size);
    bfree(hex);

    fseek(vector_file, 0, SEEK_END);
    hex = create_file_hex_hash(vector_file, 1, DIGEST_MD5, NULL);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, md5_hex_ka_0, size));
    bfree(hex);

    fseek(vector_file, 26, SEEK_SET);
    hex = create_file_hex_hash(vector_file, 1, DIGEST_MD5, NULL);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, md5_hex_ka_1, size));
    bfree(hex);

    hex = create_file_hex_hash(vector_file, 3, DIGEST_MD5, NULL);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, md5_hex_ka_2, size));
    bfree(hex);

    fseek(vector_file, 0, SEEK_SET);
    hex = create_file_hex_hash(vector_file, FILE_END, DIGEST_MD5, NULL);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, md5_hex_ka_3, size));
    bfree(hex);

    // SHA1
    fseek(vector_file, 0, SEEK_SET);
    hex = create_file_hex_hash(vector_file, 0, DIGEST_SHA1, &size);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, sha1_hex_ka_0, size));
    ck_assert_int_eq(40, size);
    bfree(hex);

    fseek(vector_file, 0, SEEK_END);
    hex = create_file_hex_hash(vector_file, 1, DIGEST_SHA1, NULL);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, sha1_hex_ka_0, size));
    bfree(hex);

    fseek(vector_file, 26, SEEK_SET);
    hex = create_file_hex_hash(vector_file, 1, DIGEST_SHA1, NULL);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, sha1_hex_ka_1, size));
    bfree(hex);

    hex = create_file_hex_hash(vector_file, 3, DIGEST_SHA1, NULL);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, sha1_hex_ka_2, size));
    bfree(hex);

    fseek(vector_file, 0, SEEK_SET);
    hex = create_file_hex_hash(vector_file, FILE_END, DIGEST_SHA1, NULL);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, sha1_hex_ka_3, size));
    bfree(hex);

    // SHA256
    fseek(vector_file, 0, SEEK_SET);
    hex = create_file_hex_hash(vector_file, 0, DIGEST_SHA256, &size);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, sha256_hex_ka_0, size));
    ck_assert_int_eq(64, size);
    bfree(hex);

    fseek(vector_file, 0, SEEK_END);
    hex = create_file_hex_hash(vector_file, 1, DIGEST_SHA256, NULL);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, sha256_hex_ka_0, size));
    bfree(hex);

    fseek(vector_file, 26, SEEK_SET);
    hex = create_file_hex_hash(vector_file, 1, DIGEST_SHA256, NULL);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, sha256_hex_ka_1, size));
    bfree(hex);

    hex = create_file_hex_hash(vector_file, 3, DIGEST_SHA256, NULL);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, sha256_hex_ka_2, size));
    bfree(hex);

    fseek(vector_file, 0, SEEK_SET);
    hex = create_file_hex_hash(vector_file, FILE_END, DIGEST_SHA256, NULL);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, sha256_hex_ka_3, size));
    bfree(hex);

    // SHA512
    fseek(vector_file, 0, SEEK_SET);
    hex = create_file_hex_hash(vector_file, 0, DIGEST_SHA512, &size);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, sha512_hex_ka_0, size));
    ck_assert_int_eq(128, size);
    bfree(hex);

    fseek(vector_file, 0, SEEK_END);
    hex = create_file_hex_hash(vector_file, 1, DIGEST_SHA512, NULL);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, sha512_hex_ka_0, size));
    bfree(hex);

    fseek(vector_file, 26, SEEK_SET);
    hex = create_file_hex_hash(vector_file, 1, DIGEST_SHA512, NULL);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, sha512_hex_ka_1, size));
    bfree(hex);

    hex = create_file_hex_hash(vector_file, 3, DIGEST_SHA512, NULL);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, sha512_hex_ka_2, size));
    bfree(hex);

    fseek(vector_file, 0, SEEK_SET);
    hex = create_file_hex_hash(vector_file, FILE_END, DIGEST_SHA512, NULL);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, sha512_hex_ka_3, size));
    bfree(hex);
}
END_TEST

START_TEST(create_mem_hex_hash_test) {
    char *hex = NULL;
    size_t size = 0;

    ck_assert(create_mem_hex_hash(NULL, 0, DIGEST_MD5, &size) == NULL);
    ck_assert(create_mem_hex_hash(vector, 0, DIGEST_INVALID, &size) == NULL);

    /// MD5
    hex = create_mem_hex_hash(vector, 0, DIGEST_MD5, &size);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, md5_hex_ka_0, size));
    ck_assert_int_eq(32, size);
    bfree(hex);

    hex = create_mem_hex_hash(vector + 26, 1, DIGEST_MD5, NULL);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, md5_hex_ka_1, size));
    bfree(hex);

    hex = create_mem_hex_hash(vector + 26, 3, DIGEST_MD5, NULL);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, md5_hex_ka_2, size));
    bfree(hex);

    hex = create_mem_hex_hash(vector, strlen(vector), DIGEST_MD5, NULL);
    ck_assert(hex != NULL);
    ck_assert_int_eq(0, memcmp(hex, md5_hex_ka_3, size));
    bfree(hex);
}
END_TEST

START_TEST(verify_buf_file_test) {
    ck_assert_int_ne(UPDATE_OK, verify_buf_file(NULL, 0, md5_buf_ka_0, DIGEST_MD5));
    ck_assert_int_ne(UPDATE_OK, verify_buf_file(vector_file, 0, NULL, DIGEST_MD5));
    ck_assert_int_ne(UPDATE_OK, verify_buf_file(vector_file, 0, md5_buf_ka_0, DIGEST_INVALID));

    fseek(vector_file, 0, SEEK_SET);
    ck_assert_int_eq(UPDATE_OK, verify_buf_file(vector_file, 0, md5_buf_ka_0, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_OK, verify_buf_file(vector_file, 0, sha1_buf_ka_0, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_OK, verify_buf_file(vector_file, 0, sha256_buf_ka_0, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_OK, verify_buf_file(vector_file, 0, sha512_buf_ka_0, DIGEST_SHA512));

    fseek(vector_file, 0, SEEK_END);
    ck_assert_int_eq(UPDATE_OK, verify_buf_file(vector_file, 1, md5_buf_ka_0, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_OK, verify_buf_file(vector_file, 1, sha1_buf_ka_0, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_OK, verify_buf_file(vector_file, 1, sha256_buf_ka_0, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_OK, verify_buf_file(vector_file, 1, sha512_buf_ka_0, DIGEST_SHA512));

    fseek(vector_file, 26, SEEK_SET);
    ck_assert_int_eq(UPDATE_OK, verify_buf_file(vector_file, 1, md5_buf_ka_1, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_OK, verify_buf_file(vector_file, 1, sha1_buf_ka_1, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_OK, verify_buf_file(vector_file, 1, sha256_buf_ka_1, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_OK, verify_buf_file(vector_file, 1, sha512_buf_ka_1, DIGEST_SHA512));

    ck_assert_int_eq(UPDATE_OK, verify_buf_file(vector_file, 3, md5_buf_ka_2, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_OK, verify_buf_file(vector_file, 3, sha1_buf_ka_2, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_OK, verify_buf_file(vector_file, 3, sha256_buf_ka_2, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_OK, verify_buf_file(vector_file, 3, sha512_buf_ka_2, DIGEST_SHA512));

    fseek(vector_file, 0, SEEK_SET);
    ck_assert_int_eq(UPDATE_OK, verify_buf_file(vector_file, FILE_END, md5_buf_ka_3, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_OK, verify_buf_file(vector_file, FILE_END, sha1_buf_ka_3, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_OK, verify_buf_file(vector_file, FILE_END, sha256_buf_ka_3, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_OK, verify_buf_file(vector_file, FILE_END, sha512_buf_ka_3, DIGEST_SHA512));

    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_buf_file(vector_file, FILE_END, md5_buf_ka_2, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_buf_file(vector_file, FILE_END, sha1_buf_ka_2, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_buf_file(vector_file, FILE_END, sha256_buf_ka_2, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_buf_file(vector_file, FILE_END, sha512_buf_ka_2, DIGEST_SHA512));

    fseek(bad_vector_file, 0, SEEK_SET);
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_buf_file(bad_vector_file, FILE_END, md5_buf_ka_3, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_buf_file(bad_vector_file, FILE_END, sha1_buf_ka_3, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_buf_file(bad_vector_file, FILE_END, sha256_buf_ka_3, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_buf_file(bad_vector_file, FILE_END, sha512_buf_ka_3, DIGEST_SHA512));
}
END_TEST

START_TEST(verify_buf_mem_test) {
    ck_assert_int_ne(UPDATE_OK, verify_buf_mem(NULL, 0, md5_buf_ka_0, DIGEST_MD5));
    ck_assert_int_ne(UPDATE_OK, verify_buf_mem(vector, 0, NULL, DIGEST_MD5));
    ck_assert_int_ne(UPDATE_OK, verify_buf_mem(vector, 0, md5_buf_ka_0, DIGEST_INVALID));

    ck_assert_int_eq(UPDATE_OK, verify_buf_mem(vector, 0, md5_buf_ka_0, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_OK, verify_buf_mem(vector, 0, sha1_buf_ka_0, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_OK, verify_buf_mem(vector, 0, sha256_buf_ka_0, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_OK, verify_buf_mem(vector, 0, sha512_buf_ka_0, DIGEST_SHA512));

    ck_assert_int_eq(UPDATE_OK, verify_buf_mem(vector + 26, 1, md5_buf_ka_1, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_OK, verify_buf_mem(vector + 26, 1, sha1_buf_ka_1, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_OK, verify_buf_mem(vector + 26, 1, sha256_buf_ka_1, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_OK, verify_buf_mem(vector + 26, 1, sha512_buf_ka_1, DIGEST_SHA512));

    ck_assert_int_eq(UPDATE_OK, verify_buf_mem(vector + 26, 3, md5_buf_ka_2, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_OK, verify_buf_mem(vector + 26, 3, sha1_buf_ka_2, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_OK, verify_buf_mem(vector + 26, 3, sha256_buf_ka_2, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_OK, verify_buf_mem(vector + 26, 3, sha512_buf_ka_2, DIGEST_SHA512));

    ck_assert_int_eq(UPDATE_OK, verify_buf_mem(vector, strlen(vector), md5_buf_ka_3, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_OK, verify_buf_mem(vector, strlen(vector), sha1_buf_ka_3, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_OK, verify_buf_mem(vector, strlen(vector), sha256_buf_ka_3, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_OK, verify_buf_mem(vector, strlen(vector), sha512_buf_ka_3, DIGEST_SHA512));

    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_buf_mem(vector, strlen(vector), md5_buf_ka_2, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_buf_mem(vector, strlen(vector), sha1_buf_ka_2, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_buf_mem(vector, strlen(vector), sha256_buf_ka_2, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_buf_mem(vector, strlen(vector), sha512_buf_ka_2, DIGEST_SHA512));

    vector[0]++;
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_buf_mem(vector, strlen(vector), md5_buf_ka_3, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_buf_mem(vector, strlen(vector), sha1_buf_ka_3, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_buf_mem(vector, strlen(vector), sha256_buf_ka_3, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_buf_mem(vector, strlen(vector), sha512_buf_ka_3, DIGEST_SHA512));
    vector[0]--;
}
END_TEST

START_TEST(verify_hex_file_test) {
    ck_assert_int_ne(UPDATE_OK, verify_hex_file(NULL, 0, md5_buf_ka_0, DIGEST_MD5));
    ck_assert_int_ne(UPDATE_OK, verify_hex_file(vector_file, 0, NULL, DIGEST_MD5));
    ck_assert_int_ne(UPDATE_OK, verify_hex_file(vector_file, 0, md5_buf_ka_0, DIGEST_INVALID));

    fseek(vector_file, 0, SEEK_SET);
    ck_assert_int_eq(UPDATE_OK, verify_hex_file(vector_file, 0, md5_hex_ka_0, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_OK, verify_hex_file(vector_file, 0, sha1_hex_ka_0, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_OK, verify_hex_file(vector_file, 0, sha256_hex_ka_0, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_OK, verify_hex_file(vector_file, 0, sha512_hex_ka_0, DIGEST_SHA512));

    fseek(vector_file, 0, SEEK_END);
    ck_assert_int_eq(UPDATE_OK, verify_hex_file(vector_file, 1, md5_hex_ka_0, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_OK, verify_hex_file(vector_file, 1, sha1_hex_ka_0, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_OK, verify_hex_file(vector_file, 1, sha256_hex_ka_0, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_OK, verify_hex_file(vector_file, 1, sha512_hex_ka_0, DIGEST_SHA512));

    fseek(vector_file, 26, SEEK_SET);
    ck_assert_int_eq(UPDATE_OK, verify_hex_file(vector_file, 1, md5_hex_ka_1, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_OK, verify_hex_file(vector_file, 1, sha1_hex_ka_1, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_OK, verify_hex_file(vector_file, 1, sha256_hex_ka_1, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_OK, verify_hex_file(vector_file, 1, sha512_hex_ka_1, DIGEST_SHA512));

    ck_assert_int_eq(UPDATE_OK, verify_hex_file(vector_file, 3, md5_hex_ka_2, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_OK, verify_hex_file(vector_file, 3, sha1_hex_ka_2, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_OK, verify_hex_file(vector_file, 3, sha256_hex_ka_2, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_OK, verify_hex_file(vector_file, 3, sha512_hex_ka_2, DIGEST_SHA512));

    fseek(vector_file, 0, SEEK_SET);
    ck_assert_int_eq(UPDATE_OK, verify_hex_file(vector_file, FILE_END, md5_hex_ka_3, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_OK, verify_hex_file(vector_file, FILE_END, sha1_hex_ka_3, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_OK, verify_hex_file(vector_file, FILE_END, sha256_hex_ka_3, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_OK, verify_hex_file(vector_file, FILE_END, sha512_hex_ka_3, DIGEST_SHA512));

    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_hex_file(vector_file, FILE_END, md5_hex_ka_2, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_hex_file(vector_file, FILE_END, sha1_hex_ka_2, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_hex_file(vector_file, FILE_END, sha256_hex_ka_2, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_hex_file(vector_file, FILE_END, sha512_hex_ka_2, DIGEST_SHA512));

    fseek(bad_vector_file, 0, SEEK_SET);
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_hex_file(bad_vector_file, FILE_END, md5_hex_ka_3, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_hex_file(bad_vector_file, FILE_END, sha1_hex_ka_3, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_hex_file(bad_vector_file, FILE_END, sha256_hex_ka_3, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_hex_file(bad_vector_file, FILE_END, sha512_hex_ka_3, DIGEST_SHA512));
}
END_TEST

START_TEST(verify_hex_mem_test) {
    ck_assert_int_ne(UPDATE_OK, verify_hex_mem(NULL, 0, md5_buf_ka_0, DIGEST_MD5));
    ck_assert_int_ne(UPDATE_OK, verify_hex_mem(vector, 0, NULL, DIGEST_MD5));
    ck_assert_int_ne(UPDATE_OK, verify_hex_mem(vector, 0, md5_buf_ka_0, DIGEST_INVALID));

    ck_assert_int_eq(UPDATE_OK, verify_hex_mem(vector, 0, md5_hex_ka_0, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_OK, verify_hex_mem(vector, 0, sha1_hex_ka_0, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_OK, verify_hex_mem(vector, 0, sha256_hex_ka_0, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_OK, verify_hex_mem(vector, 0, sha512_hex_ka_0, DIGEST_SHA512));

    ck_assert_int_eq(UPDATE_OK, verify_hex_mem(vector + 26, 1, md5_hex_ka_1, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_OK, verify_hex_mem(vector + 26, 1, sha1_hex_ka_1, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_OK, verify_hex_mem(vector + 26, 1, sha256_hex_ka_1, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_OK, verify_hex_mem(vector + 26, 1, sha512_hex_ka_1, DIGEST_SHA512));

    ck_assert_int_eq(UPDATE_OK, verify_hex_mem(vector + 26, 3, md5_hex_ka_2, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_OK, verify_hex_mem(vector + 26, 3, sha1_hex_ka_2, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_OK, verify_hex_mem(vector + 26, 3, sha256_hex_ka_2, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_OK, verify_hex_mem(vector + 26, 3, sha512_hex_ka_2, DIGEST_SHA512));

    ck_assert_int_eq(UPDATE_OK, verify_hex_mem(vector, strlen(vector), md5_hex_ka_3, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_OK, verify_hex_mem(vector, strlen(vector), sha1_hex_ka_3, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_OK, verify_hex_mem(vector, strlen(vector), sha256_hex_ka_3, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_OK, verify_hex_mem(vector, strlen(vector), sha512_hex_ka_3, DIGEST_SHA512));

    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_hex_mem(vector, strlen(vector), md5_hex_ka_2, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_hex_mem(vector, strlen(vector), sha1_hex_ka_2, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_hex_mem(vector, strlen(vector), sha256_hex_ka_2, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_hex_mem(vector, strlen(vector), sha512_hex_ka_2, DIGEST_SHA512));

    vector[0]++;
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_hex_mem(vector, strlen(vector), md5_hex_ka_3, DIGEST_MD5));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_hex_mem(vector, strlen(vector), sha1_hex_ka_3, DIGEST_SHA1));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_hex_mem(vector, strlen(vector), sha256_hex_ka_3, DIGEST_SHA256));
    ck_assert_int_eq(UPDATE_DIG_FAIL, verify_hex_mem(vector, strlen(vector), sha512_hex_ka_3, DIGEST_SHA512));
    vector[0]--;
}
END_TEST

START_TEST(verify_embedded_buf_file_test) {
    FILE *s;
    char *buf;
    size_t size;
    DigestMethod method, rmethod;
    // Note: the ck_assert_int_* macro will exec verify_embedded_buf_file twice
    // resulting any eq to fail since the FILE pointer is in the wrong place
    // after the first call.
    UpdateCode code;

    ck_assert_int_ne(UPDATE_OK, verify_embedded_buf_file(NULL, NULL));

    // Check invalid digest code.
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    buf = create_embedded_buf(md5_buf_ka_0, sizeof(md5_buf_ka_0) - 1,
            vector, 0, DIGEST_INVALID, &size);
    ck_assert(buf != NULL);
    fwrite(buf, 1, size, s);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_buf_file(s, &rmethod);
    ck_assert_int_ne(UPDATE_OK, code);
    bfree(buf);
    tmpfunclose(s);

    // Check invalid digest size (skipping first char).
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    buf = create_embedded_buf(md5_buf_ka_0 + 1, sizeof(md5_buf_ka_0) - 2,
            vector, 0, DIGEST_MD5, &size);
    ck_assert(buf != NULL);
    fwrite(buf, 1, size, s);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_buf_file(s, NULL);
    ck_assert_int_ne(UPDATE_OK, code);
    bfree(buf);
    tmpfunclose(s);

    method = DIGEST_MD5;
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    buf = create_embedded_buf(md5_buf_ka_0, sizeof(md5_buf_ka_0) - 1,
            vector, 0, method, &size);
    ck_assert(buf != NULL);
    fwrite(buf, 1, size, s);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_buf_file(s, &rmethod);
    ck_assert_int_eq(UPDATE_OK, code);
    ck_assert_int_eq(method, rmethod);
    ck_assert_int_eq(ftell(s), sizeof(md5_buf_ka_0) + 1);
    bfree(buf);
    tmpfunclose(s);

    s = tmpfopen("w+");
    ck_assert(s != NULL);
    buf = create_embedded_buf(md5_buf_ka_1, sizeof(md5_buf_ka_1) - 1,
            vector + 26, 1, method, &size);
    ck_assert(buf != NULL);
    fwrite(buf, 1, size, s);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_buf_file(s, NULL);
    ck_assert_int_eq(UPDATE_OK, code);
    bfree(buf);
    tmpfunclose(s);

    s = tmpfopen("w+");
    ck_assert(s != NULL);
    buf = create_embedded_buf(md5_buf_ka_2, sizeof(md5_buf_ka_2) - 1,
            vector + 26, 3, method, &size);
    ck_assert(buf != NULL);
    fwrite(buf, 1, size, s);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_buf_file(s, NULL);
    ck_assert_int_eq(UPDATE_OK, code);
    bfree(buf);
    tmpfunclose(s);

    s = tmpfopen("w+");
    ck_assert(s != NULL);
    buf = create_embedded_buf(md5_buf_ka_3, sizeof(md5_buf_ka_3) - 1,
            vector, strlen(vector), method, &size);
    ck_assert(buf != NULL);
    fwrite(buf, 1, size, s);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_buf_file(s, NULL);
    ck_assert_int_eq(UPDATE_OK, code);
    bfree(buf);
    tmpfunclose(s);

    // Change start of ka_3.
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    md5_buf_ka_3[0]++;
    buf = create_embedded_buf(md5_buf_ka_3, sizeof(md5_buf_ka_3) - 1,
            vector, strlen(vector), method, &size);
    md5_buf_ka_3[0]--;
    ck_assert(buf != NULL);
    fwrite(buf, 1, size, s);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_buf_file(s, NULL);
    ck_assert_int_ne(UPDATE_OK, code);
    bfree(buf);
    tmpfunclose(s);

    // Change middle of ka_3.
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    md5_buf_ka_3[sizeof(md5_buf_ka_3) / 2]++;
    buf = create_embedded_buf(md5_buf_ka_3, sizeof(md5_buf_ka_3) - 1,
            vector, strlen(vector), method, &size);
    md5_buf_ka_3[sizeof(md5_buf_ka_3) / 2]--;
    ck_assert(buf != NULL);
    fwrite(buf, 1, size, s);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_buf_file(s, NULL);
    ck_assert_int_ne(UPDATE_OK, code);
    bfree(buf);
    tmpfunclose(s);

    // Change end of ka_3.
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    md5_buf_ka_3[sizeof(md5_buf_ka_3) - 2]++;
    buf = create_embedded_buf(md5_buf_ka_3, sizeof(md5_buf_ka_3) - 1,
            vector, strlen(vector), method, &size);
    md5_buf_ka_3[sizeof(md5_buf_ka_3) - 2]--;
    ck_assert(buf != NULL);
    fwrite(buf, 1, size, s);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_buf_file(s, NULL);
    ck_assert_int_ne(UPDATE_OK, code);
    bfree(buf);
    tmpfunclose(s);

    // Change : before ka_3.
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    buf = create_embedded_buf(md5_buf_ka_3, sizeof(md5_buf_ka_3) - 1,
            vector, strlen(vector), method, &size);
    ck_assert(buf != NULL);
    buf[1]++;
    fwrite(buf, 1, size, s);
    buf[1]--;
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_buf_file(s, NULL);
    ck_assert_int_ne(UPDATE_OK, code);
    bfree(buf);
    tmpfunclose(s);

    // Mismatch ka and vector.
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    buf = create_embedded_buf(md5_buf_ka_0, sizeof(md5_buf_ka_0) - 1,
            vector, strlen(vector), method, &size);
    ck_assert(buf != NULL);
    fwrite(buf, 1, size, s);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_buf_file(s, NULL);
    ck_assert_int_ne(UPDATE_OK, code);
    bfree(buf);
    tmpfunclose(s);

    // Check remaining methods using only ka_3
    method = DIGEST_SHA1;
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    buf = create_embedded_buf(sha1_buf_ka_3, sizeof(sha1_buf_ka_3) - 1,
            vector, strlen(vector), method, &size);
    ck_assert(buf != NULL);
    fwrite(buf, 1, size, s);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_buf_file(s, &rmethod);
    ck_assert_int_eq(UPDATE_OK, code);
    ck_assert_int_eq(method, rmethod);
    ck_assert_int_eq(ftell(s), sizeof(sha1_buf_ka_3) + 1);
    bfree(buf);
    tmpfunclose(s);

    method = DIGEST_SHA256;
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    buf = create_embedded_buf(sha256_buf_ka_3, sizeof(sha256_buf_ka_3) - 1,
            vector, strlen(vector), method, &size);
    ck_assert(buf != NULL);
    fwrite(buf, 1, size, s);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_buf_file(s, &rmethod);
    ck_assert_int_eq(UPDATE_OK, code);
    ck_assert_int_eq(method, rmethod);
    ck_assert_int_eq(ftell(s), sizeof(sha256_buf_ka_3) + 1);
    bfree(buf);
    tmpfunclose(s);

    method = DIGEST_SHA512;
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    buf = create_embedded_buf(sha512_buf_ka_3, sizeof(sha512_buf_ka_3) - 1,
            vector, strlen(vector), method, &size);
    ck_assert(buf != NULL);
    fwrite(buf, 1, size, s);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_buf_file(s, &rmethod);
    ck_assert_int_eq(UPDATE_OK, code);
    ck_assert_int_eq(method, rmethod);
    ck_assert_int_eq(ftell(s), sizeof(sha512_buf_ka_3) + 1);
    bfree(buf);
    tmpfunclose(s);
}
END_TEST

START_TEST(verify_embedded_buf_mem_test) {
    char *buf;
    size_t size;
    size_t offset;
    DigestMethod method, rmethod;
    UpdateCode code;

    ck_assert_int_ne(UPDATE_OK, verify_embedded_buf_mem(NULL, 2, NULL, NULL));

    // Check invalid digest code.
    buf = create_embedded_buf(md5_buf_ka_0, sizeof(md5_buf_ka_0) - 1,
            vector, 0, DIGEST_INVALID, &size);
    ck_assert(buf != NULL);
    code = verify_embedded_buf_mem(buf, size, &rmethod, NULL);
    ck_assert_int_ne(UPDATE_OK, code);
    bfree(buf);

    // Check invalid digest size (skipping first char).
    buf = create_embedded_buf(md5_buf_ka_0 + 1, sizeof(md5_buf_ka_0) - 2,
            vector, 0, DIGEST_MD5, &size);
    ck_assert(buf != NULL);
    code = verify_embedded_buf_mem(buf, size, NULL, NULL);
    ck_assert_int_ne(UPDATE_OK, code);
    bfree(buf);

    method = DIGEST_MD5;
    buf = create_embedded_buf(md5_buf_ka_0, sizeof(md5_buf_ka_0) - 1,
            vector, 0, method, &size);
    ck_assert(buf != NULL);
    code = verify_embedded_buf_mem(buf, size, &rmethod, &offset);
    ck_assert_int_eq(UPDATE_OK, code);
    ck_assert_int_eq(method, rmethod);
    ck_assert_int_eq(offset, sizeof(md5_buf_ka_0) + 1);
    bfree(buf);

    buf = create_embedded_buf(md5_buf_ka_1, sizeof(md5_buf_ka_1) - 1,
            vector + 26, 1, method, &size);
    ck_assert(buf != NULL);
    code = verify_embedded_buf_mem(buf, size, NULL, NULL);
    ck_assert_int_eq(UPDATE_OK, code);
    bfree(buf);

    buf = create_embedded_buf(md5_buf_ka_2, sizeof(md5_buf_ka_2) - 1,
            vector + 26, 3, method, &size);
    ck_assert(buf != NULL);
    code = verify_embedded_buf_mem(buf, size, NULL, NULL);
    ck_assert_int_eq(UPDATE_OK, code);
    bfree(buf);

    buf = create_embedded_buf(md5_buf_ka_3, sizeof(md5_buf_ka_3) - 1,
            vector, strlen(vector), method, &size);
    ck_assert(buf != NULL);
    code = verify_embedded_buf_mem(buf, size, NULL, NULL);
    ck_assert_int_eq(UPDATE_OK, code);
    bfree(buf);

    // Change start of ka_3.
    md5_buf_ka_3[0]++;
    buf = create_embedded_buf(md5_buf_ka_3, sizeof(md5_buf_ka_3) - 1,
            vector, strlen(vector), method, &size);
    md5_buf_ka_3[0]--;
    ck_assert(buf != NULL);
    code = verify_embedded_buf_mem(buf, size, NULL, NULL);
    ck_assert_int_ne(UPDATE_OK, code);
    bfree(buf);

    // Change middle of ka_3.
    md5_buf_ka_3[sizeof(md5_buf_ka_3) / 2]++;
    buf = create_embedded_buf(md5_buf_ka_3, sizeof(md5_buf_ka_3) - 1,
            vector, strlen(vector), method, &size);
    md5_buf_ka_3[sizeof(md5_buf_ka_3) / 2]--;
    ck_assert(buf != NULL);
    code = verify_embedded_buf_mem(buf, size, NULL, NULL);
    ck_assert_int_ne(UPDATE_OK, code);
    bfree(buf);

    // Change end of ka_3.
    md5_buf_ka_3[sizeof(md5_buf_ka_3) - 2]++;
    buf = create_embedded_buf(md5_buf_ka_3, sizeof(md5_buf_ka_3) - 1,
            vector, strlen(vector), method, &size);
    md5_buf_ka_3[sizeof(md5_buf_ka_3) - 2]--;
    ck_assert(buf != NULL);
    code = verify_embedded_buf_mem(buf, size, NULL, NULL);
    ck_assert_int_ne(UPDATE_OK, code);
    bfree(buf);

    // Change : before ka_3.
    buf = create_embedded_buf(md5_buf_ka_3, sizeof(md5_buf_ka_3) - 1,
            vector, strlen(vector), method, &size);
    ck_assert(buf != NULL);
    buf[1]++;
    code = verify_embedded_buf_mem(buf, size, NULL, NULL);
    buf[1]--;
    ck_assert_int_ne(UPDATE_OK, code);
    bfree(buf);

    // Mismatch ka and vector.
    buf = create_embedded_buf(md5_buf_ka_0, sizeof(md5_buf_ka_0) - 1,
            vector, strlen(vector), method, &size);
    ck_assert(buf != NULL);
    code = verify_embedded_buf_mem(buf, size, NULL, NULL);
    ck_assert_int_ne(UPDATE_OK, code);
    bfree(buf);

    // Check remaining methods using only ka_3
    method = DIGEST_SHA1;
    buf = create_embedded_buf(sha1_buf_ka_3, sizeof(sha1_buf_ka_3) - 1,
            vector, strlen(vector), method, &size);
    ck_assert(buf != NULL);
    code = verify_embedded_buf_mem(buf, size, &rmethod, &offset);
    ck_assert_int_eq(UPDATE_OK, code);
    ck_assert_int_eq(method, rmethod);
    ck_assert_int_eq(offset, sizeof(sha1_buf_ka_3) + 1);
    bfree(buf);

    method = DIGEST_SHA256;
    buf = create_embedded_buf(sha256_buf_ka_3, sizeof(sha256_buf_ka_3) - 1,
            vector, strlen(vector), method, &size);
    ck_assert(buf != NULL);
    code = verify_embedded_buf_mem(buf, size, &rmethod, &offset);
    ck_assert_int_eq(UPDATE_OK, code);
    ck_assert_int_eq(method, rmethod);
    ck_assert_int_eq(offset, sizeof(sha256_buf_ka_3) + 1);
    bfree(buf);

    method = DIGEST_SHA512;
    buf = create_embedded_buf(sha512_buf_ka_3, sizeof(sha512_buf_ka_3) - 1,
            vector, strlen(vector), method, &size);
    ck_assert(buf != NULL);
    code = verify_embedded_buf_mem(buf, size, &rmethod, &offset);
    ck_assert_int_eq(UPDATE_OK, code);
    ck_assert_int_eq(method, rmethod);
    ck_assert_int_eq(offset, sizeof(sha512_buf_ka_3) + 1);
    bfree(buf);
}
END_TEST

START_TEST(verify_embedded_hex_file_test) {
    FILE *s;
    char *ka;
    const char *prefix = "# ";
    int size;
    DigestMethod method, rmethod;
    // Note: the ck_assert_int_* macro will exec verify_embedded_hex_file twice
    // resulting any eq to fail since the FILE pointer is in the wrong place
    // after the first call.
    UpdateCode code;
    char c;

    ck_assert_int_ne(UPDATE_OK, verify_embedded_hex_file(NULL, prefix, NULL));

    // Check invalid digest code.
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    size = fprintf(s, embedded_fmt, prefix, DIGEST_INVALID, md5_hex_ka_0, 0, vector);
    ck_assert_int_ne(size, -1);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_hex_file(s, prefix, &rmethod);
    ck_assert_int_ne(UPDATE_OK, code);
    tmpfunclose(s);

    // Check invalid digest size (skipping first char).
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    size = fprintf(s, embedded_fmt, prefix, DIGEST_MD5, md5_hex_ka_0 + 1, 0, vector);
    ck_assert_int_ne(size, -1);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_hex_file(s, prefix, NULL);
    ck_assert_int_ne(UPDATE_OK, code);
    tmpfunclose(s);

    // Check bad prefix.
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    size = fprintf(s, embedded_fmt, " ", DIGEST_MD5, md5_hex_ka_0, 0, vector);
    ck_assert_int_ne(size, -1);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_hex_file(s, prefix, NULL);
    ck_assert_int_ne(UPDATE_OK, code);
    tmpfunclose(s);

    method = DIGEST_MD5;
    ka = md5_hex_ka_0;
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    size = fprintf(s, embedded_fmt, prefix, method, ka, 0, vector);
    ck_assert_int_ne(size, -1);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_hex_file(s, prefix, &rmethod);
    ck_assert_int_eq(UPDATE_OK, code);
    ck_assert_int_eq(method, rmethod);
    ck_assert_int_eq(ftell(s), strlen(prefix) + strlen(ka) + 3);
    tmpfunclose(s);

    ka = md5_hex_ka_1;
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    size = fprintf(s, embedded_fmt, prefix, method, ka, 1, vector + 26);
    ck_assert_int_ne(size, -1);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_hex_file(s, prefix, NULL);
    ck_assert_int_eq(UPDATE_OK, code);
    tmpfunclose(s);

    ka = md5_hex_ka_2;
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    size = fprintf(s, embedded_fmt, prefix, method, ka, 3, vector + 26);
    ck_assert_int_ne(size, -1);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_hex_file(s, prefix, NULL);
    ck_assert_int_eq(UPDATE_OK, code);
    tmpfunclose(s);

    ka = md5_hex_ka_3;
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    size = fprintf(s, embedded_fmt, prefix, method, ka, strlen(vector), vector);
    ck_assert_int_ne(size, -1);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_hex_file(s, prefix, NULL);
    ck_assert_int_eq(UPDATE_OK, code);
    tmpfunclose(s);

    // Change start of ka_3.
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    md5_hex_ka_3[0]++;
    size = fprintf(s, embedded_fmt, prefix, method, ka, strlen(vector), vector);
    md5_hex_ka_3[0]--;
    ck_assert_int_ne(size, -1);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_hex_file(s, prefix, NULL);
    ck_assert_int_ne(UPDATE_OK, code);
    tmpfunclose(s);

    // Change middle of ka_3.
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    md5_hex_ka_3[sizeof(md5_hex_ka_3) / 2]++;
    size = fprintf(s, embedded_fmt, prefix, method, ka, strlen(vector), vector);
    md5_hex_ka_3[sizeof(md5_hex_ka_3) / 2]--;
    ck_assert_int_ne(size, -1);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_hex_file(s, prefix, NULL);
    ck_assert_int_ne(UPDATE_OK, code);
    tmpfunclose(s);

    // Change end of ka_3.
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    md5_hex_ka_3[sizeof(md5_hex_ka_3) - 2]++;
    size = fprintf(s, embedded_fmt, prefix, method, ka, strlen(vector), vector);
    md5_hex_ka_3[sizeof(md5_hex_ka_3) - 2]--;
    ck_assert_int_ne(size, -1);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_hex_file(s, prefix, NULL);
    ck_assert_int_ne(UPDATE_OK, code);
    tmpfunclose(s);

    // Change : before ka_3.
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    size = fprintf(s, embedded_fmt, prefix, method, ka, strlen(vector), vector);
    fseek(s, strlen(prefix) + 1, SEEK_SET);
    fread(&c, 1, 1, s);
    c++;
    fseek(s, -1, SEEK_CUR);
    fwrite(&c, 1, 1, s);
    ck_assert_int_ne(size, -1);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_hex_file(s, prefix, NULL);
    ck_assert_int_ne(UPDATE_OK, code);
    tmpfunclose(s);

    // Mismatch ka and vector.
    ka = md5_hex_ka_0;
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    size = fprintf(s, embedded_fmt, prefix, method, ka, strlen(vector), vector);
    ck_assert_int_ne(size, -1);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_hex_file(s, prefix, NULL);
    ck_assert_int_ne(UPDATE_OK, code);
    tmpfunclose(s);

    // Check remaining methods using only ka_3
    method = DIGEST_SHA1;
    ka = sha1_hex_ka_3;
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    size = fprintf(s, embedded_fmt, prefix, method, ka, strlen(vector), vector);
    ck_assert_int_ne(size, -1);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_hex_file(s, prefix, &rmethod);
    ck_assert_int_eq(UPDATE_OK, code);
    ck_assert_int_eq(method, rmethod);
    ck_assert_int_eq(ftell(s), strlen(prefix) + strlen(ka) + 3);
    tmpfunclose(s);

    prefix = "-- ";  // Prefix used for lua files.
    method = DIGEST_SHA256;
    ka = sha256_hex_ka_3;
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    size = fprintf(s, embedded_fmt, prefix, method, ka, strlen(vector), vector);
    ck_assert_int_ne(size, -1);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_hex_file(s, prefix, &rmethod);
    ck_assert_int_eq(UPDATE_OK, code);
    ck_assert_int_eq(method, rmethod);
    ck_assert_int_eq(ftell(s), strlen(prefix) + strlen(ka) + 3);
    tmpfunclose(s);

    method = DIGEST_SHA512;
    ka = sha512_hex_ka_3;
    s = tmpfopen("w+");
    ck_assert(s != NULL);
    size = fprintf(s, embedded_fmt, prefix, method, ka, strlen(vector), vector);
    ck_assert_int_ne(size, -1);
    fseek(s, 0, SEEK_SET);
    code = verify_embedded_hex_file(s, prefix, &rmethod);
    ck_assert_int_eq(UPDATE_OK, code);
    ck_assert_int_eq(method, rmethod);
    ck_assert_int_eq(ftell(s), strlen(prefix) + strlen(ka) + 3);
    tmpfunclose(s);
}
END_TEST

START_TEST(verify_embedded_hex_mem_test) {
    char *s = NULL;
    char *ka;
    const char *prefix = "# ";
    int size;
    DigestMethod method, rmethod;
    size_t offset;

    ck_assert_int_ne(UPDATE_OK, verify_embedded_hex_mem(NULL, 0, prefix, NULL, NULL));
    ck_assert_int_ne(UPDATE_OK, verify_embedded_hex_mem(vector, 0, NULL, NULL, NULL));

    // Check invalid digest code.
    size = asprintf(&s, embedded_fmt, prefix, DIGEST_INVALID, md5_hex_ka_0, 0, vector);
    ck_assert_int_ne(size, -1);
    ck_assert_int_ne(UPDATE_OK, verify_embedded_hex_mem(s, size, prefix, &method, &offset));
    free(s);
    s = NULL;

    // Check invalid digest size (skipping first char).
    size = asprintf(&s, embedded_fmt, prefix, DIGEST_MD5, md5_hex_ka_0 + 1, 0, vector);
    ck_assert_int_ne(size, -1);
    ck_assert_int_ne(UPDATE_OK, verify_embedded_hex_mem(s, size, prefix, NULL, NULL));
    free(s);
    s = NULL;

    // Check bad prefix.
    size = asprintf(&s, embedded_fmt, " ", DIGEST_MD5, md5_hex_ka_0, 0, vector);
    ck_assert_int_ne(size, -1);
    ck_assert_int_ne(UPDATE_OK, verify_embedded_hex_mem(s, size, prefix, NULL, NULL));
    free(s);
    s = NULL;

    method = DIGEST_MD5;
    ka = md5_hex_ka_0;
    size = asprintf(&s, embedded_fmt, prefix, method, ka, 0, vector);
    ck_assert_int_ne(size, -1);
    ck_assert_int_eq(UPDATE_OK, verify_embedded_hex_mem(s, size, prefix, &rmethod, &offset));
    ck_assert_int_eq(method, rmethod);
    ck_assert_int_eq(offset, strlen(prefix) + strlen(ka) + 3);
    free(s);
    s = NULL;

    ka = md5_hex_ka_1;
    size = asprintf(&s, embedded_fmt, prefix, method, ka, 1, vector + 26);
    ck_assert_int_ne(size, -1);
    ck_assert_int_eq(UPDATE_OK, verify_embedded_hex_mem(s, size, prefix, NULL, NULL));
    free(s);
    s = NULL;

    ka = md5_hex_ka_2;
    size = asprintf(&s, embedded_fmt, prefix, method, ka, 3, vector + 26);
    ck_assert_int_ne(size, -1);
    ck_assert_int_eq(UPDATE_OK, verify_embedded_hex_mem(s, size, prefix, NULL, NULL));
    free(s);
    s = NULL;

    ka = md5_hex_ka_3;
    size = asprintf(&s, embedded_fmt, prefix, method, ka, strlen(vector), vector);
    ck_assert_int_ne(size, -1);
    ck_assert_int_eq(UPDATE_OK, verify_embedded_hex_mem(s, size, prefix, NULL, NULL));
    free(s);
    s = NULL;

    // Change start of ka_3.
    md5_hex_ka_3[0]++;
    size = asprintf(&s, embedded_fmt, prefix, method, ka, strlen(vector), vector);
    md5_hex_ka_3[0]--;
    ck_assert_int_ne(size, -1);
    ck_assert_int_ne(UPDATE_OK, verify_embedded_hex_mem(s, size, prefix, NULL, NULL));
    free(s);
    s = NULL;

    // Change middle of ka_3.
    md5_hex_ka_3[sizeof(md5_hex_ka_3) / 2]++;
    size = asprintf(&s, embedded_fmt, prefix, method, ka, strlen(vector), vector);
    md5_hex_ka_3[sizeof(md5_hex_ka_3) / 2]--;
    ck_assert_int_ne(size, -1);
    ck_assert_int_ne(UPDATE_OK, verify_embedded_hex_mem(s, size, prefix, NULL, NULL));
    free(s);
    s = NULL;

    // Change end of ka_3.
    md5_hex_ka_3[sizeof(md5_hex_ka_3) - 2]++;
    size = asprintf(&s, embedded_fmt, prefix, method, ka, strlen(vector), vector);
    md5_hex_ka_3[sizeof(md5_hex_ka_3) - 2]--;
    ck_assert_int_ne(size, -1);
    ck_assert_int_ne(UPDATE_OK, verify_embedded_hex_mem(s, size, prefix, NULL, NULL));
    free(s);
    s = NULL;

    // Change : before ka_3.
    size = asprintf(&s, embedded_fmt, prefix, method, ka, strlen(vector), vector);
    s[strlen(prefix) + 1]++;
    ck_assert_int_ne(size, -1);
    ck_assert_int_ne(UPDATE_OK, verify_embedded_hex_mem(s, size, prefix, NULL, NULL));
    free(s);
    s = NULL;

    // Mismatch ka and vector.
    ka = md5_hex_ka_0;
    size = asprintf(&s, embedded_fmt, prefix, method, ka, strlen(vector), vector);
    ck_assert_int_ne(size, -1);
    ck_assert_int_ne(UPDATE_OK, verify_embedded_hex_mem(s, size, prefix, NULL, NULL));
    free(s);
    s = NULL;

    // Check remaining methods using only ka_3
    method = DIGEST_SHA1;
    ka = sha1_hex_ka_3;
    size = asprintf(&s, embedded_fmt, prefix, method, ka, strlen(vector), vector);
    ck_assert_int_ne(size, -1);
    ck_assert_int_eq(UPDATE_OK, verify_embedded_hex_mem(s, size, prefix, &rmethod, &offset));
    ck_assert_int_eq(method, rmethod);
    ck_assert_int_eq(offset, strlen(prefix) + strlen(ka) + 3);
    free(s);
    s = NULL;

    prefix = "-- ";  // Prefix used for lua files.
    method = DIGEST_SHA256;
    ka = sha256_hex_ka_3;
    size = asprintf(&s, embedded_fmt, prefix, method, ka, strlen(vector), vector);
    ck_assert_int_ne(size, -1);
    ck_assert_int_eq(UPDATE_OK, verify_embedded_hex_mem(s, size, prefix, &rmethod, &offset));
    ck_assert_int_eq(method, rmethod);
    ck_assert_int_eq(offset, strlen(prefix) + strlen(ka) + 3);
    free(s);
    s = NULL;

    method = DIGEST_SHA512;
    ka = sha512_hex_ka_3;
    size = asprintf(&s, embedded_fmt, prefix, method, ka, strlen(vector), vector);
    ck_assert_int_ne(size, -1);
    ck_assert_int_eq(UPDATE_OK, verify_embedded_hex_mem(s, size, prefix, &rmethod, &offset));
    ck_assert_int_eq(method, rmethod);
    ck_assert_int_eq(offset, strlen(prefix) + strlen(ka) + 3);
    free(s);
    s = NULL;
}
END_TEST


Suite *verify_suite(void) {
    vector_file = tmpfopen("w+");
    assert(vector_file);
    fwrite(vector, 1, strlen(vector), vector_file);
    fseek(vector_file, 0, SEEK_SET);

    bad_vector_file = tmpfopen("w+");
    assert(bad_vector_file);
    fwrite("bad", 1, 3, bad_vector_file);
    fwrite(vector, 1, strlen(vector), bad_vector_file);
    fseek(bad_vector_file, 0, SEEK_SET);

    Suite *s = suite_create("verify");

    TCase *tc_core = tcase_create("core");
    tcase_add_test(tc_core, create_file_buf_hash_test);
    tcase_add_test(tc_core, create_mem_buf_hash_test);
    tcase_add_test(tc_core, create_file_hex_hash_test);
    tcase_add_test(tc_core, create_mem_hex_hash_test);
    tcase_add_test(tc_core, verify_buf_file_test);
    tcase_add_test(tc_core, verify_buf_mem_test);
    tcase_add_test(tc_core, verify_hex_file_test);
    tcase_add_test(tc_core, verify_hex_mem_test);
    tcase_add_test(tc_core, verify_embedded_buf_file_test);
    tcase_add_test(tc_core, verify_embedded_buf_mem_test);
    tcase_add_test(tc_core, verify_embedded_hex_file_test);
    tcase_add_test(tc_core, verify_embedded_hex_mem_test);
    suite_add_tcase(s, tc_core);

    return s;
}

void cleanup_verify_suite(void) {
    tmpfunclose(vector_file);
    tmpfunclose(bad_vector_file);
}
