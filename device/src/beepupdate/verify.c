#include <errno.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include "beepupdate.h"
#include "yaml_utils.h"

#define FILE_CHUNK_SIZE                             (16 * 1024)
#define SIGNED_MAGIC                                (0xb5b9debc)

static const char lua_script_prefix[] = "-- ";
static const char sh_yml_script_prefix[] = "# ";

static UpdateCode evp_md_file_md_value(FILE *stream, size_t size,
        const EVP_MD *md, unsigned char *md_value, unsigned int *md_size) {
    EVP_MD_CTX mdctx;
    uint8_t *buf;
    long start_pos;
    size_t remaining;
    size_t rsize;
    UpdateCode rcode = UPDATE_ERR;

    if (!stream)
        return UPDATE_BAD_ARG;

    buf = (uint8_t *)bmalloc(FILE_CHUNK_SIZE);
    if (!buf)
        return UPDATE_ERR;

    start_pos = ftell(stream);
    fseek(stream, 0, SEEK_END);
    remaining = ftell(stream) - start_pos;
    remaining = (remaining < size) ? remaining : size;
    fseek(stream, start_pos, SEEK_SET);

    EVP_MD_CTX_init(&mdctx);
    EVP_DigestInit_ex(&mdctx, md, NULL);

    while (remaining) {
        rsize = (remaining < FILE_CHUNK_SIZE) ? remaining : FILE_CHUNK_SIZE;
        rsize = fread(buf, 1, rsize, stream);
        if (!rsize)
            break;
        if (!EVP_DigestUpdate(&mdctx, buf, rsize))
            break;
        remaining -= rsize;
    }

    if (!remaining && EVP_DigestFinal_ex(&mdctx, md_value, md_size) == 1) {
        rcode = UPDATE_OK;
    }

    EVP_MD_CTX_cleanup(&mdctx);

    memset(&mdctx, 0, sizeof(mdctx));
    bfree(buf);
    buf = NULL;

    fseek(stream, start_pos, SEEK_SET);

    return rcode;
}

static UpdateCode evp_md_mem_md_value(const void *data, size_t size,
        const EVP_MD *md, unsigned char *md_value, unsigned int *md_size) {
    EVP_MD_CTX mdctx;
    UpdateCode rcode = UPDATE_ERR;

    if (!data)
        return UPDATE_BAD_ARG;

    EVP_MD_CTX_init(&mdctx);
    EVP_DigestInit_ex(&mdctx, md, NULL);

    if (EVP_DigestUpdate(&mdctx, data, size) &&
            EVP_DigestFinal_ex(&mdctx, md_value, md_size)) {
        rcode = UPDATE_OK;
    }

    EVP_MD_CTX_cleanup(&mdctx);

    memset(&mdctx, 0, sizeof(mdctx));

    return rcode;
}

static const EVP_MD *get_evp(DigestMethod method) {
    switch (method) {
    case DIGEST_MD5:
        return EVP_md5();
    case DIGEST_SHA1:
        return EVP_sha1();
    case DIGEST_SHA256:
        return EVP_sha256();
    case DIGEST_SHA512:
        return EVP_sha512();

    // The signed methods use the same EVP_MD but instead of calling into
    // EVP_Digest* we use EVP_Verify* functions.
    case DIGEST_SIGNED_SHA256:
        return EVP_sha256();
    case DIGEST_SIGNED_SHA512:
        return EVP_sha512();
    default:
        return NULL;
    }
}

// Keep inline with DigestMethod enum.
static DigestMethod code_to_method(char code) {
    switch (code) {
        case '0': return DIGEST_MD5;
        case '1': return DIGEST_SHA1;
        case '2': return DIGEST_SHA256;
        case '3': return DIGEST_SHA512;
        case '4': return DIGEST_SIGNED_SHA256;
        case '5': return DIGEST_SIGNED_SHA512;
        default: return DIGEST_INVALID;
    }
}

static size_t hash_to_hex_size(DigestMethod method) {
    switch (method) {
    case DIGEST_MD5: return 32;
    case DIGEST_SHA1: return 40;
    case DIGEST_SHA256: return 64;
    case DIGEST_SHA512: return 128;
    default: return 0;
    }
}

// Returns 0 on error.
static size_t ftype_hdr_size(FileType ftype) {
    switch(ftype) {
    case FILE_TYPE_BINARY:
        // 'MMMM:CCCC'
        return 9;

    case FILE_TYPE_LUA:
        // '-- MMMMMMMM:CCCCCCCC\n'
        return 21;

    case FILE_TYPE_SH:
    case FILE_TYPE_YML:
        // '# MMMMMMMM:CCCCCCCC\n'
        return 20;

    case FILE_TYPE_MANIFEST:
        return 0;  // We should never call this for manifests.

    default:
        return 0;  // Error.
    }
}

static UpdateCode read_dig_size_mem(const void *data, size_t size,
        FileType ftype, uint32_t *dig_size) {
    const char *prefix;
    const char *buf = data;
    size_t hdr_size = ftype_hdr_size(ftype);
    uint32_t magic;
    uint32_t read_size;

    if (!data || !dig_size || !hdr_size) {
        return UPDATE_BAD_ARG;
    }

    if (ftype == FILE_TYPE_BINARY) {
        // 'MMMM:CCCC'
        if (buf[4] != ':') {
            return UPDATE_DIG_FAIL;
        }
        memcpy(&magic, buf, sizeof(uint32_t));
        memcpy(&read_size, buf + 5, sizeof(uint32_t));
    } else {
        // '-- MMMMMMMM:CCCCCCCC\n'
        // '# MMMMMMMM:CCCCCCCC\n'
        if (ftype == FILE_TYPE_LUA) {
            prefix = lua_script_prefix;
        } else {
            prefix = sh_yml_script_prefix;
        }
        // This does assume prefix is a null terminated string.  We only
        // ever use this in human readable files but will need to be
        // changed if we use prefixes in other binary files.
        if (memcmp(prefix, buf, strlen(prefix))) {
            return UPDATE_DIG_FAIL;
        }

        // Count backwards since it is the same for all file types.
        if ((buf[hdr_size - 1] != '\n') || (buf[hdr_size - 10] != ':')) {
            return UPDATE_DIG_FAIL;
        }

        hex_to_buf(&magic, buf + hdr_size - 18, sizeof(magic) * 2);
        hex_to_buf(&read_size, buf + hdr_size - 9, sizeof(read_size) * 2);
    }

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    magic = __builtin_bswap32(magic);
    read_size = __builtin_bswap32(read_size);
#endif

    // Check the magic.
    if (magic != SIGNED_MAGIC) {
        return UPDATE_DIG_FAIL;
    }

    *dig_size = read_size;
    return UPDATE_OK;
}

// read_dig_list_count_* will reset the file position indicator to 0L and
// will leave it right after the magic, count values.
static UpdateCode read_dig_size_file(FILE *stream, FileType ftype,
        uint32_t *dig_size) {
    uint8_t buf[MAX_DIGEST_HEX_SIZE];
    size_t hdr_size = ftype_hdr_size(ftype);
    size_t rsize;

    if (!stream || !dig_size || !hdr_size) {
        return UPDATE_BAD_ARG;
    }

    if (hdr_size > sizeof(buf)) {
        LOG_ERROR("internal error");
        return UPDATE_ERR;
    }

    if (fseek(stream, 0L, SEEK_SET) == -1) {
        LOG_ERROR("could not seek: %s", strerror(errno));
        return UPDATE_FILE_ERR;
    }

    rsize = fread(buf, 1, hdr_size, stream);
    if (rsize != hdr_size) {
        return UPDATE_DIG_FAIL;
    }

    return read_dig_size_mem(buf, hdr_size, ftype, dig_size);
}

static UpdateCode create_dig_entry(const void *data, size_t size, FileType ftype,
        size_t *entry_size, DigestEntry **dig_entry) {
    const const char *buf = data;
    DigestEntry *new_entry = NULL;
    const char *prefix = NULL;
    size_t prefix_size = 0;
    size_t min_size = 0;
    size_t dig_size_bytes = 2;
    size_t bin_dig_size;
    DigestMethod dig_method = DIGEST_INVALID;
    uint16_t dig_size;
    bool trailing_newline = false;
    bool dig_in_hex = false;

    if (!data || !size || !entry_size || !dig_entry) {
        return UPDATE_BAD_ARG;
    }

    switch (ftype) {
    case FILE_TYPE_BINARY:
        break;

    case FILE_TYPE_LUA:
    case FILE_TYPE_SH:
    case FILE_TYPE_YML:
        if (ftype == FILE_TYPE_LUA) {
            prefix = lua_script_prefix;
        } else {
            prefix = sh_yml_script_prefix;
        }
        prefix_size = strlen(prefix);
        dig_size_bytes = 4;
        trailing_newline = true;
        dig_in_hex = true;
        break;

    case FILE_TYPE_MANIFEST:
        dig_size_bytes = 4;
        dig_in_hex = true;
        break;

    default:
        return UPDATE_BAD_ARG;
    }

    // Check if there is enough space to read out the dig type and size.
    min_size = 3;  // 1 byte for dig type, 2 bytes for separating colons.
    min_size += prefix_size;
    min_size += trailing_newline ? 1 : 0;
    // size is 2 bytes in binary, 4 bytes in hex.
    min_size += dig_size_bytes;

    // This leaves 0 space for the actual digest, but it is safe to read
    // out the size now.
    if (min_size > size) {
        return UPDATE_DIG_FAIL;
    }

    // Digest size is going to be after prefix + code (1 byte) + ':'.
    if (dig_in_hex) {
        hex_to_buf(&dig_size, buf + prefix_size + 2, sizeof(dig_size) * 2);
    } else {
        memcpy(&dig_size, &buf[prefix_size + 2], sizeof(uint16_t));
    }

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    dig_size = __builtin_bswap16(dig_size);
#endif

    // Make sure there is enough space with the given dig size.
    if ((min_size + dig_size) > size) {
        return UPDATE_DIG_FAIL;
    }

    // Digest type is going to right after prefix.
    dig_method = code_to_method(buf[prefix_size]);
    if (dig_method == DIGEST_INVALID) {
        // To support future compatibility with newer methods
        // we should ignore ones we do not know about.  Return
        // not found so the caller can move on to the next digest
        // entry of fail out if necessary.
        *dig_entry = NULL;
        *entry_size = min_size + dig_size;
        return UPDATE_NOT_FOUND;
    }

    // The size of the buffer is now valid.  Do the rest of the checks.
    if (memcmp(prefix, buf, prefix_size)
        || buf[prefix_size + 1] != ':'
        || buf[prefix_size + dig_size_bytes + 2] != ':') {
        return UPDATE_DIG_FAIL;
    }

    if (trailing_newline
            && buf[min_size + dig_size - 1] != '\n') {
        return UPDATE_DIG_FAIL;
    }

    // Everything checks out create the dig_entry.
    new_entry = bmalloc(sizeof(DigestEntry));
    if (!new_entry) {
        return UPDATE_OOM;
    }

    // If the digest is in hex we only need half the bytes to store it in
    // binary.
    bin_dig_size = dig_in_hex ? dig_size / 2 : dig_size;

    // If this is a hash make sure it is the correct size.
    if (is_hash(dig_method)
            && ((hash_to_hex_size(dig_method) / 2) != bin_dig_size)) {
        return UPDATE_DIG_FAIL;
    }

    new_entry->data = bmalloc(bin_dig_size);
    if (!new_entry->data) {
        bfree(new_entry);
        return UPDATE_OOM;
    }

    new_entry->dig_size = bin_dig_size;
    new_entry->dig_method = dig_method;
    if (dig_in_hex) {
        hex_to_buf(new_entry->data, buf + prefix_size + dig_size_bytes + 3,
                dig_size);
    } else {
        memcpy(new_entry->data, buf + prefix_size + dig_size_bytes + 3,
                bin_dig_size);
    }

    *dig_entry = new_entry;
    *entry_size = min_size + dig_size;

    return UPDATE_OK;
}

// It is important that this keeps the order of the dig_list the same
// as they are read from the file.  This allows us to have ordered
// priority.
static UpdateCode combine_dig_lists(DigestEntry **current_dig_list,
        DigestEntry *new_dig_list) {
    if (!current_dig_list || !new_dig_list) {
        return UPDATE_BAD_ARG;
    }

    // *current_dig_list may be NULL if this is a new list.
    if (*current_dig_list) {
        // If a list already exists add the new list to the end.
        DigestEntry *tail = *current_dig_list;
        while (tail->next)
            tail = tail->next;
        tail->next = new_dig_list;
    } else {
        *current_dig_list = new_dig_list;
    }

    return UPDATE_OK;
}

static UpdateCode create_dig_list(const void *data, size_t size,
        FileType ftype, DigestEntry **dig_list) {
    const uint8_t *buf = data;
    DigestEntry *new_dig_entry = NULL;
    size_t spent_size = 0;
    size_t entry_size;
    UpdateCode rcode = UPDATE_OK;
    int entry_count = 0;

    if (!data || !size || !dig_list) {
        return UPDATE_BAD_ARG;
    }

    while (spent_size < size && rcode == UPDATE_OK) {
        new_dig_entry = NULL;
        rcode = create_dig_entry(buf, size - spent_size, ftype, &entry_size,
                &new_dig_entry);
        // Only add new_dig_entry on OK.
        if (rcode == UPDATE_OK) {
            combine_dig_lists(dig_list, new_dig_entry);
        }
        // Still continue through the list if we get NOT_FOUND meaning
        // the method was not found and was added for a future beepupdate.
        if (rcode == UPDATE_OK || rcode == UPDATE_NOT_FOUND) {
            spent_size += entry_size;
            buf += entry_size;
            entry_count++;
            rcode = UPDATE_OK;
        }
    }

    if (!entry_count) {
        rcode = UPDATE_DIG_FAIL;
    }

    return rcode;
}

static void *create_hash_mem(const void *data, size_t size,
        DigestMethod method, size_t *md_size, UpdateCode *code) {
    const EVP_MD *md = get_evp(method);
    void *md_value = NULL;
    unsigned int r_md_size = 0;
    UpdateCode rcode;

    if (!data || !size || !is_hash(method)) {
        rcode = UPDATE_BAD_ARG;
        goto done;
    }

    md_value = bmalloc(EVP_MAX_MD_SIZE);
    if (!md_value) {
        rcode = UPDATE_OOM;
        goto done;
    }

    rcode = evp_md_mem_md_value(data, size, md, md_value, &r_md_size);

    if (rcode != UPDATE_OK) {
        bfree(md_value);
        md_value = NULL;
    }

done:
    if (md_size)
        *md_size = r_md_size;

    if (code)
        *code = rcode;

    return md_value;
}

static UpdateCode verify_hash_mem(const void *data, size_t size,
        DigestEntry *dig_entry) {
    void *md_data;
    size_t md_size;
    UpdateCode code = UPDATE_ERR;

    if (!data || !size || !dig_entry || !is_hash(dig_entry->dig_method) ||
            !dig_entry->data || !dig_entry->dig_size) {
        return UPDATE_BAD_ARG;
    }

    md_data = create_hash_mem(data, size, dig_entry->dig_method, &md_size,
            &code);

    if (!md_data) {
        return code;
    }

    if (md_size != dig_entry->dig_size || memcmp(md_data, dig_entry->data,
            dig_entry->dig_size)) {
        code = UPDATE_DIG_FAIL;
    }

    bfree(md_data);

    return code;
}

static void *create_hash_file(FILE *stream, DigestMethod method,
        size_t *md_size, UpdateCode *code) {
    const EVP_MD *md = get_evp(method);
    void *md_value = NULL;
    unsigned int r_md_size = 0;
    UpdateCode rcode;

    if (!stream || !is_hash(method)) {
        rcode = UPDATE_BAD_ARG;
        goto done;
    }

    md_value = bmalloc(EVP_MAX_MD_SIZE);
    if (!md_value) {
        rcode = UPDATE_OOM;
        goto done;
    }

    rcode = evp_md_file_md_value(stream, FILE_END, md, md_value, &r_md_size);
    if (rcode != UPDATE_OK) {
        bfree(md_value);
        md_value = NULL;
    }

done:
    if (md_size)
        *md_size = r_md_size;

    if (code)
        *code = rcode;

    return md_value;
}

static UpdateCode verify_hash_file(FILE *stream,
        DigestEntry *dig_entry) {
    void *md_data;
    size_t md_size;
    long start_pos;
    UpdateCode code = UPDATE_ERR;

    if (!stream || !dig_entry || !is_hash(dig_entry->dig_method) ||
            !dig_entry->data || !dig_entry->dig_size) {
        return UPDATE_BAD_ARG;
    }

    start_pos = ftell(stream);

    md_data = create_hash_file(stream, dig_entry->dig_method, &md_size,
            &code);

    fseek(stream, start_pos, SEEK_SET);

    if (!md_data) {
        return code;
    }

    if (md_size != dig_entry->dig_size || memcmp(md_data, dig_entry->data,
            dig_entry->dig_size)) {
        code = UPDATE_DIG_FAIL;
    }

    bfree(md_data);

    return code;
}

static void ssl_key_data_free(RSA *rsa_pub_key, EVP_PKEY *pkey) {
    if (pkey) {
        EVP_PKEY_free(pkey);
    }

    if (rsa_pub_key) {
        RSA_free(rsa_pub_key);
    }
}

static UpdateCode get_ssl_key_data(uint32_t key_id, RSA **rsa_pub_key,
        EVP_PKEY **pkey) {
    const PublicKey *public_key;
    const unsigned char *key_data;
    UpdateCode rcode = UPDATE_SSL_ERR;

    if (!rsa_pub_key || !pkey) {
        return UPDATE_BAD_ARG;
    }

    // NULL these out so if an error happens we don't try to free bad
    // addresses.
    *rsa_pub_key = NULL;
    *pkey = NULL;

    // We may not have the key for this particular signature.  This will
    // allow us to revoke keys later yet still sign with the old key for
    // devices which have not been updated in a while.
    public_key = get_public_key(key_id);
    if (!public_key) {
        return UPDATE_NOT_FOUND;
    }

    // The key table should be const so it's protected by the ro flags in
    // the MMU.  Need to have a second pointer that is compatible with
    // d2i_RSA_PUBKEY.
    key_data = public_key->key;

    // Get the keys from the binary DER format into something openssl
    // can use for the verify process.
    *rsa_pub_key = d2i_RSA_PUBKEY(NULL, &key_data, public_key->key_size);
    if (!(*rsa_pub_key)) {
        goto done;
    }

    *pkey = EVP_PKEY_new();
    if (!(*pkey)) {
        goto done;
    }

    if (EVP_PKEY_set1_RSA(*pkey, *rsa_pub_key) != 1) {
        goto done;
    }

    rcode = UPDATE_OK;

done:
    if (rcode != UPDATE_OK) {
        ssl_key_data_free(*rsa_pub_key, *pkey);
        // Make sure the caller knows these have been freed.
        *rsa_pub_key = NULL;
        *pkey = NULL;
    }

    return rcode;
}

static UpdateCode verify_signature_mem(const void *data, size_t size,
        DigestEntry *dig_entry) {
    EVP_MD_CTX mdctx;
    const EVP_MD *md;
    RSA *rsa_pub_key = NULL;
    EVP_PKEY *pkey = NULL;
    uint8_t *signature_buf;
    UpdateCode rcode = UPDATE_OK;
    uint32_t key_id;

    if (!data || !size || !dig_entry
            || !is_signature(dig_entry->dig_method) || !dig_entry->data
            || dig_entry->dig_size <= 4) {
        return UPDATE_BAD_ARG;
    }

    md = get_evp(dig_entry->dig_method);
    if (!md) {
        return UPDATE_DIG_FAIL;
    }

    signature_buf = dig_entry->data;

    // The first 4 bytes of the signature is the key id used to create it.
    memcpy(&key_id, signature_buf, sizeof(uint32_t));
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    key_id = __builtin_bswap32(key_id);
#endif

    rcode = get_ssl_key_data(key_id, &rsa_pub_key, &pkey);
    if (rcode != UPDATE_OK) {
        // On error the key data will be freed.
        return rcode;
    }

    EVP_MD_CTX_init(&mdctx);
    if (EVP_VerifyInit_ex(&mdctx, md, NULL) != 1) {
        EVP_MD_CTX_cleanup(&mdctx);
        return UPDATE_SSL_ERR;
    }

    if (!EVP_VerifyUpdate(&mdctx, data, size)) {
        rcode = UPDATE_SSL_ERR;
    }

    // Adjust 4 bytes in the signature_buf for the key_id.
    if (rcode == UPDATE_OK && (EVP_VerifyFinal(&mdctx, signature_buf + 4,
            dig_entry->dig_size - 4, pkey) != 1)) {
        rcode = UPDATE_DIG_FAIL;
    }

    if (rcode == UPDATE_OK) {
        LOG_DEBUG("verified using key: %08x", key_id);
    }

    EVP_MD_CTX_cleanup(&mdctx);
    memset(&mdctx, 0, sizeof(mdctx));

    ssl_key_data_free(rsa_pub_key, pkey);

    return rcode;
}

static UpdateCode verify_signature_file(FILE *stream,
        DigestEntry *dig_entry) {
    EVP_MD_CTX mdctx;
    const EVP_MD *md;
    RSA *rsa_pub_key = NULL;
    EVP_PKEY *pkey = NULL;
    uint8_t *data_buf = NULL;
    uint8_t *signature_buf;
    size_t rsize;
    long start_pos;
    long remaining;
    UpdateCode rcode = UPDATE_DIG_FAIL;
    uint32_t key_id;

    if (!stream || !dig_entry || !is_signature(dig_entry->dig_method) ||
            !dig_entry->data || dig_entry->dig_size <= 4) {
        return UPDATE_BAD_ARG;
    }

    md = get_evp(dig_entry->dig_method);
    if (!md) {
        return UPDATE_DIG_FAIL;
    }

    signature_buf = dig_entry->data;

    // The first 4 bytes of the signature is the key id used to create it.
    memcpy(&key_id, signature_buf, sizeof(uint32_t));
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    key_id = __builtin_bswap32(key_id);
#endif

    rcode = get_ssl_key_data(key_id, &rsa_pub_key, &pkey);
    if (rcode != UPDATE_OK) {
        // On error the key data will be freed.
        return rcode;
    }

    data_buf = (uint8_t *)bmalloc(FILE_CHUNK_SIZE);
    if (!data_buf) {
        rcode = UPDATE_OOM;
        goto done;
    }

    // Find out how much data we need.
    start_pos = ftell(stream);
    fseek(stream, 0, SEEK_END);
    remaining = ftell(stream) - start_pos;
    fseek(stream, start_pos, SEEK_SET);

    EVP_MD_CTX_init(&mdctx);
    if (EVP_VerifyInit_ex(&mdctx, md, NULL) != 1) {
        EVP_MD_CTX_cleanup(&mdctx);
        return UPDATE_SSL_ERR;
    }

    rcode = UPDATE_DIG_FAIL;

    while (remaining) {
        rsize = (remaining < FILE_CHUNK_SIZE) ? remaining : FILE_CHUNK_SIZE;
        rsize = fread(data_buf, 1, rsize, stream);
        if (!rsize) {
            rcode = UPDATE_FILE_ERR;
            break;
        }
        if (!EVP_VerifyUpdate(&mdctx, data_buf, rsize)) {
            rcode = UPDATE_SSL_ERR;
            break;
        }
        remaining -= rsize;
    }

    // Adjust 4 bytes in the signature_buf for the key_id.
    if (!remaining && (EVP_VerifyFinal(&mdctx, signature_buf + 4,
            dig_entry->dig_size - 4, pkey) == 1)) {
        rcode = UPDATE_OK;
    }

    if (rcode == UPDATE_OK) {
        LOG_DEBUG("verified using key: %08x", key_id);
    }

    EVP_MD_CTX_cleanup(&mdctx);
    memset(&mdctx, 0, sizeof(mdctx));

    fseek(stream, start_pos, SEEK_SET);

done:
    ssl_key_data_free(rsa_pub_key, pkey);

    if (data_buf) {
        bfree(data_buf);
        data_buf = NULL;
    }

    return rcode;
}

bool is_hash(DigestMethod method) {
    switch (method) {
    case DIGEST_MD5:
    case DIGEST_SHA1:
    case DIGEST_SHA256:
    case DIGEST_SHA512:
        return true;
    default:
        return false;
    }
}

bool is_signature(DigestMethod method) {
    switch (method) {
    case DIGEST_SIGNED_SHA256:
    case DIGEST_SIGNED_SHA512:
        return true;
    default:
        return false;
    }
}

bool is_ftype(FileType ftype) {
    switch(ftype) {
    case FILE_TYPE_BINARY:
    case FILE_TYPE_LUA:
    case FILE_TYPE_SH:
    case FILE_TYPE_YML:
        return true;
    default:
        return false;
    }
}

void dig_list_free(DigestEntry *dig_list) {
    DigestEntry *next_dig;
    while (dig_list) {
        next_dig = dig_list->next;
        if (dig_list->data)
            bfree(dig_list->data);
        bfree(dig_list);
        dig_list = next_dig;
    }
}

UpdateCode read_dig_list_mem(const void *data, size_t size, FileType ftype,
        size_t *offset, DigestEntry **dig_list) {
    DigestEntry *new_dig_list = NULL;
    const char *buf = data;
    size_t hdr_size = ftype_hdr_size(ftype);
    uint32_t dig_size = 0;
    UpdateCode rcode;

    if (!data || !offset || !dig_list || !hdr_size) {
        return UPDATE_BAD_ARG;
    }

    rcode = read_dig_size_mem(buf, size, ftype, &dig_size);
    if (rcode != UPDATE_OK) {
        return rcode;
    }

    if ((hdr_size + dig_size) > size) {
        return UPDATE_DIG_FAIL;
    }

    rcode = create_dig_list(buf + hdr_size, dig_size, ftype, &new_dig_list);
    if (rcode != UPDATE_OK) {
        dig_list_free(new_dig_list);
        new_dig_list = NULL;
    } else {
        combine_dig_lists(dig_list, new_dig_list);
        *offset = hdr_size + dig_size;
    }

    return rcode;
}

UpdateCode read_dig_list_file(FILE *stream, FileType ftype,
        DigestEntry **dig_list) {
    DigestEntry *new_dig_list = NULL;
    uint8_t *dig_buf = NULL;
    size_t rsize;
    UpdateCode rcode;
    uint32_t dig_size;

    if (!stream || !is_ftype(ftype) || !dig_list)
        return UPDATE_BAD_ARG;

    // This will reset the stream to 0L before reading the header.
    rcode = read_dig_size_file(stream, ftype, &dig_size);
    if (rcode != UPDATE_OK) {
        goto done;
    }

    dig_buf = bmalloc(dig_size);
    if (!dig_buf) {
        return UPDATE_OOM;
    }

    // stream should be right after the header.
    rsize = fread(dig_buf, 1, dig_size, stream);
    if (rsize != dig_size) {
        rcode = UPDATE_DIG_FAIL;
        goto done;
    }

    rcode = create_dig_list(dig_buf, dig_size, ftype, &new_dig_list);

    if (rcode != UPDATE_OK) {
        // Can handle NULL.
        dig_list_free(new_dig_list);
        new_dig_list = NULL;
    } else {
        combine_dig_lists(dig_list, new_dig_list);
    }

done:
    if (dig_buf)
        bfree(dig_buf);

    return rcode;
}

UpdateCode read_dig_entry_manifest(const void *data, size_t size,
        DigestEntry **dig_list) {
    DigestEntry *new_dig_entry = NULL;
    size_t entry_size;
    UpdateCode rcode = create_dig_entry(data, size, FILE_TYPE_MANIFEST,
            &entry_size, &new_dig_entry);

    // There was extra data in the entry data although it passed all of
    // the checks this is an error.
    if (rcode == UPDATE_OK && entry_size != size) {
        dig_list_free(new_dig_entry);
        rcode = UPDATE_DIG_FAIL;
    } else if (rcode == UPDATE_OK) {
        combine_dig_lists(dig_list, new_dig_entry);
    }

    return rcode;
}

UpdateCode verify_embedded_mem(const void *data, size_t size,
        FileType ftype, bool require_secure, DigestMethod *method) {
    const uint8_t *buf = data;
    DigestEntry *dig_list = NULL;
    DigestEntry *dig_entry = NULL;
    size_t offset = 0;
    UpdateCode rcode;

    if (!data || !is_ftype(ftype)) {
        return UPDATE_BAD_ARG;
    }

    rcode = read_dig_list_mem(data, size, ftype, &offset, &dig_list);
    if (rcode != UPDATE_OK) {
        return rcode;
    }

    // Adjust the size and buf to point directly after the digest data.
    buf += offset;
    size -= offset;

    rcode = UPDATE_DIG_FAIL;
    dig_entry = dig_list;
    while (dig_entry) {
        // Skip hashes if this requires secure.
        if (require_secure && is_hash(dig_entry->dig_method)) {
            dig_entry = dig_entry->next;
            continue;
        }

        if (is_hash(dig_entry->dig_method)) {
            // If we allow hashes for verification break after the first hash
            // as this should never fail.
            rcode = verify_hash_mem(buf, size, dig_entry);
            break;
        } else if (is_signature(dig_entry->dig_method)) {
            rcode = verify_signature_mem(buf, size, dig_entry);
            // verify_signature_mem UPDATE_NOT_FOUND will return if the
            // key was not found.  This is OK as we might add new methods and
            // signatures to reject keys but allows backward compatibility.
            if (rcode == UPDATE_NOT_FOUND) {
                continue;
            }
            // Else we return the failure or UPDATE_OK.
            break;
        }

        dig_entry = dig_entry->next;
    }

    // No keys or methods were found, return UPDATE_DIG_FAIL.
    if (!dig_entry) {
        rcode = UPDATE_DIG_FAIL;
    }

    if (rcode == UPDATE_OK && method) {
        *method = dig_entry->dig_method;
    }

    if (dig_list)
        dig_list_free(dig_list);

    return rcode;
}

UpdateCode verify_embedded_file(FILE *stream, FileType ftype,
        bool require_secure, DigestMethod *method) {
    DigestEntry *dig_list = NULL;
    DigestEntry *dig_entry = NULL;
    long start_pos;
    UpdateCode rcode;

    if (!stream || !is_ftype(ftype)) {
        return UPDATE_BAD_ARG;
    }

    rcode = read_dig_list_file(stream, ftype, &dig_list);
    if (rcode != UPDATE_OK) {
        goto done;
    }

    // read_dig_list_file will reset the stream to 0L and if leave it
    // directly after the digest data.
    start_pos = ftell(stream);

    rcode = UPDATE_DIG_FAIL;
    dig_entry = dig_list;
    while (dig_entry) {
        // Skip hashes if this requires secure.
        if (require_secure && is_hash(dig_entry->dig_method)) {
            dig_entry = dig_entry->next;
            continue;
        }

        // Reset stream back to the beginning of data.
        fseek(stream, start_pos, SEEK_SET);

        // Purposefully do not error out if this is an unknown hash
        // or signature type.  This allows for backward compatibility if
        // we add additional methods later.
        if (is_hash(dig_entry->dig_method)) {
            // If we allow hashes for verification break after the first hash
            // as this should never fail.
            rcode = verify_hash_file(stream, dig_entry);
            break;
        } else if (is_signature(dig_entry->dig_method)) {
            rcode = verify_signature_file(stream, dig_entry);
            // verify_signature_file UPDATE_NOT_FOUND will return if the
            // key was not found.  This is OK as we might add new methods and
            // signatures to reject keys but allows backward compatibility.
            if (rcode == UPDATE_NOT_FOUND) {
                dig_entry = dig_entry->next;
                continue;
            }
            // Else we return the failure or UPDATE_OK.
            break;
        }

        dig_entry = dig_entry->next;
    }

    // No keys or methods were found, return UPDATE_DIG_FAIL.
    if (!dig_entry) {
        rcode = UPDATE_DIG_FAIL;
    }

    if (rcode == UPDATE_OK && method) {
        *method = dig_entry->dig_method;
    }

done:
    if (dig_list)
        dig_list_free(dig_list);

    return rcode;
}
