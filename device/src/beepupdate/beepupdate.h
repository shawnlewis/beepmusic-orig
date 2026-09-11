#ifndef BEEP_UPDATE_H
#define BEEP_UPDATE_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

#define LOG_GLOBAL_CATEGORY log_beep_main
#include "beep/log.h"

#define STATE_PRIMARY_PART                          (1<<0)
#define STATE_RECOVERY_PART                         (1<<1)
#define STATE_SYSLOG_READY                          (1<<2)
#define STATE_NETWORK_READY                         (1<<3)
#define STATE_FS_READY                              (1<<4)

typedef enum {
    UPDATE_CMD_NONE = 0,  // Don't use.
    UPDATE_CMD_STATUS,
    UPDATE_CMD_UPDATE,
    UPDATE_CMD_BOOT_ACTION,
    UPDATE_CMD_BEEP_STOP,
    UPDATE_CMD_BEEP_START,
    UPDATE_CMD_BOOTCOUNT,
    UPDATE_CMD_RESET_BOOTCOUNT
} BeepUpdateCommand;

typedef struct {
    char *update_serv;
    char *update_port;
    char *device_id;
    char *device_auth;
    char *sys_ver;
    char *beep_ver;
    char *requested;
    char *prefix;
    uint32_t state;
    bool check_unless_force;
    BeepUpdateCommand cmd;
} UpdateSystemConfig;

extern UpdateSystemConfig *sysconfig;

// Only append to this from now on.
typedef enum {
    UPDATE_OK = 0,
    UPDATE_ERR = 1,
    UPDATE_BAD_ARG,
    UPDATE_CONFIG_INCOMPLETE,
    UPDATE_CURL_ERR,
    UPDATE_DIG_FAIL,
    UPDATE_FILE_ERR,
    UPDATE_HTTP_204,
    UPDATE_HTTP_ERR,
    UPDATE_INVALID_STATE,
    UPDATE_NOT_FOUND,
    UPDATE_OOM,
    UPDATE_OOR,
    UPDATE_PACKAGE_DIG_FAIL,
    UPDATE_SCRIPT_NONZERO,
    UPDATE_SCRIPT_TERM,
    UPDATE_SSL_ERR,
    UPDATE_TAR_ERR,
    UPDATE_UCI_ERR,
    UPDATE_UP_TO_DATE,
    UPDATE_YAML_ERR,
    UPDATE_ZLIB_ERR
} UpdateCode;


/// Crt
void system_config_cleanup(void);
void config_init(int argc, char **argv);
void libraries_cleanup(void);
void libraries_init(void);


/// Config
UpdateCode load_uci_config(UpdateSystemConfig *syscfg);
UpdateCode load_sw_versions(UpdateSystemConfig *syscfg);


/// Keys
typedef struct {
    const void *key;
    long key_size;
    uint32_t id;
} PublicKey;

// Returns NULL if id is not found.
const PublicKey *get_public_key(uint32_t id);


/// Verify
// Only append to this from now on.
typedef enum {
    DIGEST_MD5 = '0',
    DIGEST_SHA1 = '1',
    DIGEST_SHA256 = '2',
    DIGEST_SHA512 = '3',
    DIGEST_SIGNED_SHA256 = '4',
    DIGEST_SIGNED_SHA512 = '5',
    DIGEST_INVALID = 'z'
} DigestMethod;

typedef enum {
    FILE_TYPE_UNKNOWN = 0,  // Don't use.
    FILE_TYPE_BINARY,
    FILE_TYPE_LUA,
    FILE_TYPE_MANIFEST,  // Internally used by verify.c
    FILE_TYPE_SH,
    FILE_TYPE_YML
} FileType;

typedef struct DigestEntry DigestEntry;

struct DigestEntry {
    void *data;
    DigestEntry *next;
    size_t dig_size;
    DigestMethod dig_method;
};

// TODOJOE: Remove me later.
// We never use this and never will.  Remove this functionality.
#define FILE_END                                    ((size_t)-1)

#define MAX_HEADER_SIZE                             (21)
#define MAX_DIGEST_HEX_SIZE                         (128)

// Check if hash (i.e. something we can create on the device).
bool is_hash(DigestMethod method);
bool is_signature(DigestMethod method);
bool is_ftype(FileType ftype);
void dig_list_free(DigestEntry *dig_list);
// *offset is the offset of data right after the digest list.
// *dig_list must be NULL or a valid DigestEntry.
UpdateCode read_dig_list_mem(const void *data, size_t size, FileType ftype,
        size_t *offset, DigestEntry **dig_list);
// Sets the current value of the file position indicator in stream to right
// after the digest list.  Before reading stream this will set position to 0L.
// *dig_list must be NULL or a valid DigestEntry.
UpdateCode read_dig_list_file(FILE *stream, FileType ftype,
        DigestEntry **dig_list);
// This is used for extracting digest data embedded in manifests.
UpdateCode read_dig_entry_manifest(const void *data, size_t size,
        DigestEntry **dig_list);
// Verify embedded digests from files and memory.  If method is not NULL the
// method used for verification will be returned.  If these return error
// method will not be modified.
UpdateCode verify_embedded_mem(const void *data, size_t size,
        FileType ftype, bool require_secure, DigestMethod *method);
UpdateCode verify_embedded_file(FILE *stream, FileType ftype,
        bool require_secure, DigestMethod *method);


/// Manifest
typedef struct {
    uint64_t last_modified;
    int count;
    bool modified;
} UpdateManifest;

typedef enum {
    UMITEM_INVALID = 0,  // Must be zero.
    UMITEM_FILE,
    UMITEM_REMOVE,
    UMITEM_SH_SCRIPT,
    UMITEM_LUA_SCRIPT,
    UMITEM_PRI_FIRMWARE,
    UMITEM_REC_FIRMWARE
} UMItemType;

// Use INVALID_MASK to ensure any set flag is not ignored if it is not
// supported.
#define UMITEM_FLAGS_INVALID                        (0x80000000)
#define UMITEM_FLAGS_FILE_INVALID_MASK              (0xffffffff)
#define UMITEM_FLAGS_REMOVE_INVALID_MASK            (0xfffffff8)
#define UMITEM_FLAGS_REMOVE_CONT_ON_ERR             (0x00000001)
#define UMITEM_FLAGS_REMOVE_ALLOW_DIRS              (0x00000002)
// ALLOW_RECURSIVE implies ALLOW_DIRS.
#define UMITEM_FLAGS_REMOVE_ALLOW_RECURSIVE         (0x00000004)
#define UMITEM_FLAGS_SH_SCRIPT_INVALID_MASK         (0xffffffff)
#define UMITEM_FLAGS_LUA_SCRIPT_INVALID_MASK        (0xffffffff)
#define UMITEM_FLAGS_PRI_FIRMWARE_INVALID_MASK      (0xffffffff)
#define UMITEM_FLAGS_REC_FIRMWARE_INVALID_MASK      (0xffffffff)

typedef struct {
    DigestEntry *dig_list;
    char *path;
    uint32_t flags;
    UMItemType type;
} UMItem;

#define INSERT_FIRST (-1)
#define INSERT_LAST  (-2)

void dump_manifest(UpdateManifest *manifest);
// stream must remain open if manifest is going to be saved to the same file.
UpdateManifest *open_file_manifest(FILE *stream, UpdateCode *code);
UpdateManifest *open_mem_manifest(const void *data, size_t size,
        UpdateCode *code);
// Returns stream argument or original stream if open_file_manifest was used
// set to mode "r".  If an IO error occurs returns NULL and all streams will
// be closed.  stream and method are optional if manifest was opened from
// a file.
FILE *save_manifest(UpdateManifest *manifest, FILE *stream,
        DigestMethod method, UpdateCode *code);
UpdateCode close_manifest(UpdateManifest *manifest);
UMItem *manifest_item_new(void);
// Only used for items never connected to a manifest.
UpdateCode manifest_item_free(UMItem *item);
UMItem *manifest_get(UpdateManifest *manifest, int index, UpdateCode *code);
UpdateCode manifest_get_index(UpdateManifest *manifest, UMItem *item,
        int *index);
UpdateCode manifest_delete(UpdateManifest *manifest, UMItem *item);
// Performs insert after index or special indexes FIRST or LAST.
// Note: items can only belong to a single manifest at a time (simpler memory
// management).
UpdateCode manifest_insert(UpdateManifest *manifest, UMItem *item, int index);


/// Scripts
UpdateCode run_shell_script_file(const char *file, int *status);
UpdateCode run_shell_script_mem(const void *data, size_t size, int *status);
UpdateCode run_lua_script_file(const char *file, int *status);
UpdateCode run_lua_script_mem(const void *data, size_t size, int *status);


/// Package
typedef struct UpdatePackage UpdatePackage;

void dump_package(UpdatePackage *pkg);
UpdatePackage *open_package(const char *path, UpdateCode *code);
UpdateCode close_package(UpdatePackage *pkg);

// Get information of the current file in the package.
// Returning UPDATE_OK and **itemp == NULL signals end of package.
UpdateCode package_stat(UpdatePackage *pkg, UMItem **itemp);
UpdateCode package_skip(UpdatePackage *pkg);
// ramdisk is true if root point to a ramdisk.  If false extra steps are taken
// for safer extraction.
UpdateCode package_extract(UpdatePackage *pkg, const char *root, bool ramdisk);
// Execute the script (script local path will be /tmp).
UpdateCode package_exec(UpdatePackage *pkg, int *status);
// Process an entire package.
// Entries marked as file will be extracted.
// Entries marked as rm will be removed.
// Entries marked as sh or lua will be exec.
// Entries marked as pfirm/rfirm will be flashed (only if package only
// contains a single entry for the firmware).
UpdateCode package_process(UpdatePackage *pkg, const char *root, int *status,
        bool ramdisk);


/// Server
typedef enum {
    SCHEDULE_INVALID = 0,  // Must be zero.
    SCHEDULE_NORMAL,
    SCHEDULE_IMMEDIATE,
    SCHEDULE_DATE
} UpdateConfigSchedule;

typedef enum {
    UPDATE_GET_INVALID = 0,  // Must be zero.
    UPDATE_GET_PREINST,
    UPDATE_GET_INSTALL,
    UPDATE_GET_POSTINST
} UpdateGetSelection;

typedef struct {
    char *server;
    char *preinst;
    char *install;
    char *postinst;
    char *release;
    UpdateConfigSchedule schedule;
    bool force;
} UpdateConfig;

void dump_config(UpdateConfig *uc);
void cleanup_config(UpdateConfig *uc);
UpdateConfig *server_get_config(const char *server, const char *port,
        UpdateCode *code);
char *server_get_file(UpdateConfig *uc, UpdateGetSelection selection,
        UpdateCode *code);


/// System
#define BOOTCOUNT_OFFSET                            (0x8000)
#define BOOTCOUNT_SIZE                              (0x8000)
#define BOOTCOUNT_SECTOR_SIZE                       (0x10000)
#if (BOOTCOUNT_OFFSET + BOOTCOUNT_SIZE) != BOOTCOUNT_SECTOR_SIZE
#error "bootcount area must complete sector size"
#endif

void update_system_state(void);
UpdateCode stop_beep_services(void);
void start_beep_services(void);
UpdateCode clear_bootcount(void);
UpdateCode reset_bootcount(void);
void system_reset(void) __attribute__((__noreturn__));


/// Utils
// dest if NULL will be allocated else requires (size * 2) + 1.
char *buf_to_hex(char *dest, const void *src, size_t size);
// dest if NULL will be allocated else requires (size + 1) / 2.
void *hex_to_buf(void *dest, const char *src, size_t size);
bool strict_hex_to_uint32(uint32_t *dest, const char *src);

FILE *tmpfopen(const char *mode);
int tmpfunclose(FILE *stream);
char *tmpfpath(FILE *stream);
UpdateCode ftouch(const char *path);

char *fpath(FILE *stream);
char *join_path(const char *path1, const char *path2);
// mode will return 0 on error if ignoring the return code.
// errno set on failure.
UpdateCode fmode(const char *path, mode_t *mode);
// DO NOT USE THIS if you are changing the uid/gid.
UpdateCode check_access(const char *path, int mode);
// safe_* functions will check if the file exists before trying to
// unlink/remove.  unlink will only delete files, remove will
// delete files and directories (if empty).
// errno set on failure.
UpdateCode safe_unlink(const char *path);
UpdateCode safe_remove(const char *path);
// Does equivalent of 'rm -rf path' until something fails.
// beepupdate runs as root; you have been warned.
// Setting continue_on_error will disable the initial access check pass
// and will continue removing files until done.
UpdateCode recursive_remove(const char *path, int *count,
        bool continue_on_error);
// Equivalent of 'mkdir -p <path>'.
int mkdirr(const char *path, mode_t mode);

UpdateCode update_log_init(void);
void update_log_cleanup(void);


/// Mem
void *bmalloc(size_t size) __attribute__((malloc)) __attribute__((__warn_unused_result__));
void bfree(void *ptr);
char *bstrdup(const char *s) __attribute__((malloc)) __attribute__((nonnull (1)));
char *bstrndup(const char *s, size_t n) __attribute__((malloc)) __attribute__((nonnull (1)));

#endif  // BEEP_UPDATE_H
