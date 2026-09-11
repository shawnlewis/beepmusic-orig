#define _XOPEN_SOURCE 500
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "beepupdate.h"

static const char hex_char[] = "0123456789abcdef";

#define NFTW_MAX_FDS                                (64)

// Don't use with *c++
#define CHAR_TO_VAL(c) \
        ((c >= '0' && c <= '9') ? (c - '0') : (c >= 'a' && c <= 'f') ? \
        (c - 'a' + 10) : (c >= 'A' && c <= 'F') ? (c - 'A' + 10) : 0)

// Don't use with *c++, returns 0xf0 if invalid.
#define CHAR_TO_UINT32(c) \
        ((c >= '0' && c <= '9') ? (c - '0') : (c >= 'a' && c <= 'f') ? \
        (c - 'a' + 10) : (c >= 'A' && c <= 'F') ? (c - 'A' + 10) : 0xf0)


char *buf_to_hex(char *dest, const void *src, size_t size) {
    char *d;
    uint8_t *s = (uint8_t *)src;

    if (dest == NULL) {
        dest = (char *)bmalloc((size * 2) + 1);
    }
    d = dest;

    if (d != NULL) {
        while (size--) {
            *d++ = hex_char[*s >> 4];
            *d++ = hex_char[*s++ & 0xf];
        }
        *d = '\0';
    }

    return dest;
}

void *hex_to_buf(void *dest, const char *src, size_t size) {
    uint8_t *d;

    if (dest == NULL) {
        dest = bmalloc((size + 1) / 2);
    }
    d = (uint8_t *)dest;

    if (d != NULL) {
        d += (size + 1) / 2;
        while (size) {
            size--;
            d--;
            *d = CHAR_TO_VAL(*(src + size));
            if (size > 0) {
                size--;
                *d |= CHAR_TO_VAL(*(src + size)) << 4;
            }
        }
    }

    return dest;
}

bool strict_hex_to_uint32(uint32_t *dest, const char *src) {
    uint32_t val = 0;
    uint32_t t;
    int count = 8;

    if (!dest || !src) {
        return false;
    }

    if (src[0] == '0' && src[1] == 'x') {
        src += 2;
    }

    while (count--) {
        t = CHAR_TO_UINT32(*src);
        if (t > 0xf) {
            return false;
        }
        val <<= 4;
        val += t;
        src++;
    }

    *dest = val;
    return true;
}

static const char tmpf_template[] = "/tmp/beepupdate-XXXXXX";

FILE *tmpfopen(const char *mode) {
    char *tmpf = bmalloc(sizeof(tmpf_template));
    FILE *stream;
    int fd;

    if (!tmpf)
        return NULL;
    strncpy(tmpf, tmpf_template, sizeof(tmpf_template));

    fd = mkstemp(tmpf);
    if (fd == -1)
        return NULL;

    stream = fdopen(fd, mode);

    bfree(tmpf);
    tmpf = NULL;

    return stream;
}

int tmpfunclose(FILE *stream) {
    char *fp = tmpfpath(stream);
    if (fp) {
        unlink(fp);
        bfree(fp);
    }
    return fclose(stream);
}

// Return a path that fits in buffer size (including '\0').
static char *fpathn(FILE *stream, size_t size) {
    char *fp;
    char *proc_fd;
    int fd = fileno(stream);

    if (fd == -1)
        return NULL;

    fp = bmalloc(size);
    if (!fp)
        return NULL;

    // needs to fit /proc/self/fd/-2147483648
    proc_fd = bmalloc(26);
    if (!proc_fd) {
        bfree(fp);
        return NULL;
    }
    snprintf(proc_fd, 26, "/proc/self/fd/%d", fd);

    if (readlink(proc_fd, fp, size) == -1) {
        bfree(fp);
        fp = NULL;
    }
    bfree(proc_fd);

    return fp;
}

char *tmpfpath(FILE *stream) {
    char *fp = fpathn(stream, sizeof(tmpf_template));
    if (fp) {
        if (strncmp(tmpf_template, fp, 16)) {
            bfree(fp);
            fp = NULL;
        }
    }
    return fp;
}

UpdateCode ftouch(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
    if (fd == -1)
        return UPDATE_FILE_ERR;
    close(fd);

    // Update times so we know when the last time beepupdate actually did the
    // check.
    if (utimes(path, NULL) == -1)
        return UPDATE_FILE_ERR;

    return UPDATE_OK;
}

char *fpath(FILE *stream) {
    return fpathn(stream, 512);
}

char *join_path(const char *path1, const char *path2) {
    char *full_path;
    size_t path1_size;
    size_t full_size;

    if (!path1 || !path2)
        return NULL;

    full_size = path1_size = strlen(path1);
    // Possible extra slash and '\0'.
    full_size += + strlen(path2) + 2;

    full_path = (char *)bmalloc(full_size);
    if (full_path)
        sprintf(full_path, "%s%s%s", path1,
                (path1 != 0 && path1[path1_size - 1] != '/') ? "/" : "",
                path2);

    return full_path;
}

UpdateCode fmode(const char *path, mode_t *mode) {
    struct stat sh;

    if (!mode) {
        return UPDATE_BAD_ARG;
    }

    if (lstat(path, &sh) == 0) {
        *mode = sh.st_mode;
        return UPDATE_OK;
    }

    *mode = (mode_t)0;

    return UPDATE_FILE_ERR;
}

// DO NOT USE THIS if you are changing the uid/gid.
UpdateCode check_access(const char *path, int mode) {
    struct stat sh;
    int gmode;
    uid_t uid;
    gid_t gid;

    if (!path) {
        return UPDATE_BAD_ARG;
    }

    if (lstat(path, &sh)) {
        return UPDATE_FILE_ERR;
    }

    // Just checking if file exists.
    if (mode == F_OK) {
        return UPDATE_OK;
    }

    // Actually need to test permissions.
    mode &= (R_OK | W_OK | X_OK);

    uid = getuid();
    gid = getgid();

    // This makes the assumption that mode for access is the same
    // as st_mode returned by lstat.  It is for linux.
    if (sh.st_uid == uid) {
        gmode = ((sh.st_mode >> 6) & (R_OK | W_OK | X_OK));
    } else if (sh.st_gid == gid) {
        gmode = ((sh.st_mode >> 3) & (R_OK | W_OK | X_OK));
    } else {
        gmode = (sh.st_mode & (R_OK | W_OK | X_OK));
    }

    if ((gmode & mode) == mode)
        return UPDATE_OK;

    return UPDATE_FILE_ERR;
}

static UpdateCode safe_unlink_remove(const char *path, bool use_unlink) {
    if (check_access(path, W_OK) == UPDATE_OK) {
        int rcode = 0;
        if (use_unlink) {
            rcode = unlink(path);
        } else {
            rcode = remove(path);
        }
        if (rcode != 0) {
            return UPDATE_FILE_ERR;
        }
    } else if (errno != ENOENT) {
        // Return error for all errors except ENOENT.
        return UPDATE_FILE_ERR;
    }
    return UPDATE_OK;
}

UpdateCode safe_unlink(const char *path) {
    return safe_unlink_remove(path, true);
}

UpdateCode safe_remove(const char *path) {
    return safe_unlink_remove(path, false);
}

// This is not thread safe, use __thread if needed later.
static bool rr_cont_on_error = false;
static int rr_count = 0;

// Always report errors since this should not be called if continue_on_error
// is set.
static int rr_nftw_check_cb(const char *fpath, const struct stat *sh,
        int typeflag, struct FTW *ftwbuf) {
    UpdateCode rcode = check_access(fpath, W_OK);
    if (rcode != UPDATE_OK)
        LOG_DEBUG("write access failed on: %s with code: %d", fpath, rcode);
    return (int)rcode;
}

static int rr_nftw_remove_cb(const char *fpath, const struct stat *sh,
        int typeflag, struct FTW *ftwbuf) {
    UpdateCode rcode = safe_remove(fpath);
    if (rcode != UPDATE_OK) {
        LOG_DEBUG("remove failed on: %s", fpath);
    } else {
        rr_count++;
    }

    return rr_cont_on_error ? 0 : (int)rcode;
}

UpdateCode recursive_remove(const char *path, int *count,
        bool continue_on_error) {
    mode_t mode;
    UpdateCode rcode = UPDATE_OK;
    int rcount = 0;

    if (!path)
        return UPDATE_BAD_ARG;

    rcode = fmode(path, &mode);
    if (rcode != UPDATE_OK) {
        // Don't return error if it's not found.
        if (errno == ENOENT)
            rcode = UPDATE_OK;
        goto done;
    }

    // If this is not a directory just call safe_unlink.
    if (!S_ISDIR(mode)) {
        rcode = safe_unlink(path);
        if (rcode == UPDATE_OK) {
            rcount = 1;
        }
        goto done;
    }

    // If this is a directory we have to file tree walk to remove everything
    // inside of it.
    // If we are not going to continue on error first first check that we
    // have write access to everything first.  This takes more time but
    // is less destructive if we are going to error out.  This is not perfect
    // as a new file could be added in the mean time.
    if (!continue_on_error) {
        // ntfw will return 0 or the return value of the callback if it
        // returns non-zero.
        if (nftw(path, rr_nftw_check_cb, NFTW_MAX_FDS, FTW_DEPTH | FTW_PHYS)) {
            rcode = UPDATE_FILE_ERR;
            goto done;
        }
    }

    rr_count = 0;
    rr_cont_on_error = continue_on_error;

    if (nftw(path, rr_nftw_remove_cb, NFTW_MAX_FDS, FTW_DEPTH | FTW_PHYS)) {
        rcode = UPDATE_FILE_ERR;
    }

    rcount = rr_count;

done:
    if (count)
        *count = rcount;

    if (continue_on_error) {
        if (rcode != UPDATE_OK)
            LOG_INFO("deleted %d files but encountered errors", rcount);
        return UPDATE_OK;
    } else {
        if (rcode != UPDATE_OK)
            LOG_ERROR("deleted %d files before aborting due to errors", rcount);
        return rcode;
    }
}

int mkdirr(const char *path, mode_t mode) {
    char *p, *t;
    size_t size;
    bool ok = true;

    p = bstrdup(path);
    if (!p)
        return -1;

    size = strlen(p);
    if (size == 0 || (size == 1 && p[0] == '/')) {
        bfree(p);
        return (int)(size - 1);
    }

    if (p[size - 1] == '/')
        p[size - 1] = '\0';

    t = p + 1;
    while(*t && ok) {
        if (*t == '/') {
            *t = '\0';
            if (access(p, F_OK) == -1) {
                if (errno != ENOENT || mkdir(p, mode) != 0) {
                    ok = false;
                }
            }
            *t = '/';
        }
        t++;
    }
    if (ok && access(p, F_OK) == -1 && errno == ENOENT)
        ok = (mkdir(p, mode) == 0);

    bfree(p);

    return ok ? 0 : -1;
}

UpdateCode update_log_init(void) {
    log_beep_main = LOG_CATEGORY_GET("beepupdate");
    LOG_CATEGORY_SET_PRIORITY(log_beep_main, LOG_PRIORITY_DEBUG);

    if (sysconfig->state & STATE_SYSLOG_READY) {
        LOG_SET_SYSLOG_IDENT("beepupdate");
        LOG_SET_SYSLOG_PRIORITY(LOG_PRIORITY_DEBUG);
    }

    return UPDATE_OK;
}

void update_log_cleanup(void) {
    LOG_CLEANUP();
}
