#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "beepupdate.h"
#include "package_priv.h"

#define PACKAGE_MKDIR_MODE (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)

#define TAR_MAGIC_OFFSET 0x101
static const uint8_t tar_magic[] = {'u', 's', 't', 'a', 'r', ' ', ' ', '\0'};
#define GZ_MAGIC_OFFSET 0x0
static const uint8_t gz_magic[] = {0x1f, 0x8b};

int perrno;


static UpdatePackage *init_package_streams(const char *path,
        UpdateCode *code) {
    UpdatePackage *pkg = NULL;
    FILE *stream;
    UpdateCode (*package_io_open)(UpdatePackage *pkg, FILE *stream) = NULL;
    long pos;
    uint8_t magic_buf[8];
    UpdateCode rcode = UPDATE_ERR;

    stream = fopen(path, "r");
    if (!stream) {
        rcode = UPDATE_FILE_ERR;
        goto done;
    }

    rcode = verify_embedded_file(stream, FILE_TYPE_BINARY, true, NULL);
    if (rcode != UPDATE_OK) {
        fclose(stream);
        goto done;
    }

    pkg = (UpdatePackage *)bmalloc(sizeof(UpdatePackage));
    if (!pkg) {
        fclose(stream);
        rcode = UPDATE_OOM;
        goto done;
    }

    pkg->path = bstrdup(path);
    if (!pkg->path) {
        rcode = UPDATE_OOM;
        goto done;
    }

    // It is important to have the file position set right after the embedded
    // digests.  The *_io_open functions will use this as the zero point.
    pos = ftell(stream);
    // GZ_MAGIC_OFFSET == 0.
    if (fread(magic_buf, 1, sizeof(gz_magic), stream) != sizeof(gz_magic)) {
        rcode = UPDATE_FILE_ERR;
        goto done;
    }
    if (!memcmp(magic_buf, gz_magic, sizeof(gz_magic))) {
        package_io_open = package_zlib_open;
    }

    if (!package_io_open) {
        fseek(stream, pos + TAR_MAGIC_OFFSET, SEEK_SET);
        if (fread(magic_buf, 1, sizeof(tar_magic), stream) != sizeof(tar_magic)) {
            rcode = UPDATE_FILE_ERR;
            goto done;
        }
        if (!memcmp(magic_buf, tar_magic, sizeof(tar_magic))) {
            package_io_open = package_file_open;
        }
    }

    if (package_io_open) {
        fseek(stream, pos, SEEK_SET);
        rcode = package_io_open(pkg, stream);
    } else {
        LOG_ERROR("unknown package type");
        rcode = UPDATE_INVALID_STATE;
    }

done:
    if (rcode != UPDATE_OK && pkg) {
        close_package(pkg);
        pkg = NULL;
    }

    if (code)
        *code = rcode;

    return pkg;
}

static inline long pktell(UpdatePackage *pkg) {
    return pkg->io.tell(pkg);
}

static inline int pkseek(UpdatePackage *pkg, long offset, int whence) {
    return pkg->io.seek(pkg, offset, whence);
}

static inline size_t pkread(void *ptr, size_t size, UpdatePackage *pkg) {
    return pkg->io.read(ptr, size, pkg);
}

static bool empty_tar_header(struct header_gnu_tar *gthdr) {
    uint32_t *val = (uint32_t *)gthdr;
    int index;

    for (index = 0;
            index < sizeof(struct header_gnu_tar) / sizeof(uint32_t);
            index++) {
        if (val[index])
            return false;
    }
    return true;
}

static bool verify_tar_header(struct header_gnu_tar *gthdr) {
    uint32_t *val = (uint32_t *)gthdr;
    char *ccheck;
    long checksum;
    uint32_t calcsum = 0x20 * 8;  // 8 space chars for checksum member.
    int index;

    // Actually checks magic and version members.
    if (memcmp(gthdr->magic, tar_magic, sizeof(tar_magic))) {
        LOG_ERROR("invalid magic");
        return false;
    }

    checksum = strtol(gthdr->checksum, &ccheck, 8);
    if (*ccheck != '\0') {
        LOG_ERROR("bad checksum");
        return false;
    }

    for (index = 0;
            index < sizeof(struct header_gnu_tar) / sizeof(uint32_t);
            index++) {
        // offsetof(struct header_gnu_tar, checksum) / sizeof(uint32_t) == 37
        // sizeof(((struct header_gnu_tar *)0)->checksum) / sizeof(uint32_t) == 2
        if (index != 37 && index != 38) {
            calcsum += val[index] & 0xff;
            calcsum += (val[index] >> 8) & 0xff;
            calcsum += (val[index] >> 16) & 0xff;
            calcsum += (val[index] >> 24) & 0xff;
        }
    }

    // Max checksum in octal 0373410 which fits in checksum field (without
    // leading 0).  Checksum is 8 byte field but requires a null terminator
    // followed by a space char.  No need to truncate calcsum.
    if (calcsum != (uint32_t)checksum) {
        LOG_ERROR("invalid checksum: %lx calculated: %x", checksum, calcsum);
        return false;
    }

    return true;
}

static void dump_tar_header(UpdateTarHeader *uthdr) {
    if (!uthdr) {
        LOG_ERROR("uthdr is NULL");
        return;
    }

    LOG_INFO("UpdateTarHeader: %p", uthdr);
    LOG_INFO("  uthdr->name: %s", uthdr->name);
    LOG_INFO("  uthdr->linkname: %s", uthdr->linkname);
    LOG_INFO("  uthdr->uname: %s", uthdr->uname);
    LOG_INFO("  uthdr->gname: %s", uthdr->gname);
    LOG_INFO("  uthdr->mode: %o", uthdr->mode);
    LOG_INFO("  uthdr->uid: %d", uthdr->uid);
    LOG_INFO("  uthdr->gid: %d", uthdr->gid);
    LOG_INFO("  uthdr->size: %zu", uthdr->size);
    LOG_INFO("  uthdr->mtime: %ld", uthdr->mtime);
    LOG_INFO("  uthdr->type: %d", uthdr->type);
    LOG_INFO("  uthdr->tar_hdr_offset: %zu", uthdr->tar_hdr_offset);
    LOG_INFO("  uthdr->tar_data_offset: %zu", uthdr->tar_data_offset);
}

static void cleanup_tar_header(UpdateTarHeader *uthdr) {
    if (!uthdr)
        return;

    if (uthdr->name)
        bfree(uthdr->name);
    if (uthdr->linkname)
        bfree(uthdr->linkname);
    if (uthdr->uname)
        bfree(uthdr->uname);
    if (uthdr->gname)
        bfree(uthdr->gname);
    bfree(uthdr);
}

static void *extract_tar_to_mem(UpdatePackage *pkg, UpdateTarHeader *uthdr,
        void *dest, UpdateCode *code) {
    void *buf;
    size_t pos;
    UpdateCode rcode = UPDATE_FILE_ERR;

    if (dest)
        buf = dest;
    else
        buf = bmalloc(uthdr->size);

    if (buf) {
        if (pkseek(pkg, uthdr->tar_data_offset, SEEK_SET) == 0 &&
                pkread(buf, uthdr->size, pkg) == uthdr->size) {
            pos = uthdr->tar_data_offset + uthdr->size;
            rcode = UPDATE_OK;
        }
    } else {
        rcode = UPDATE_OOM;
    }

    // Seek to the next 512 block.
    if (rcode == UPDATE_OK && NOT_TAR_BLOCK(pos)) {
        pos = NEXT_TAR_BLOCK(pos);
        pkseek(pkg, pos, SEEK_SET);
    }

    // Free on error when buf was malloced.
    if (rcode != UPDATE_OK && buf && !dest) {
        bfree(buf);
        buf = NULL;
    }

    if (code)
        *code = rcode;

    return buf;
}

static UpdateCode extract_tar_to_stream(UpdatePackage *pkg,
        UpdateTarHeader *uthdr, FILE *stream) {
    void *buf = bmalloc(FILE_CHUNK_SIZE);
    size_t pos;
    size_t remaining = uthdr->size;
    size_t rsize;
    UpdateCode code = UPDATE_OK;

    if (!buf)
        return UPDATE_OOM;

    if (pkseek(pkg, uthdr->tar_data_offset, SEEK_SET) != 0) {
        bfree(buf);
        return UPDATE_FILE_ERR;
    }

    while (remaining) {
        rsize = (remaining < FILE_CHUNK_SIZE) ? remaining : FILE_CHUNK_SIZE;
        rsize = pkread(buf, rsize, pkg);
        if (!rsize || (fwrite(buf, 1, rsize, stream) != rsize)) {
            code = UPDATE_FILE_ERR;
            break;
        }
        remaining -= rsize;
    }

    // Seek to the next 512 block only if all data was written.
    if (!remaining && code == UPDATE_OK) {
        pos = uthdr->tar_data_offset + uthdr->size;
        if (NOT_TAR_BLOCK(pos)) {
            pos = NEXT_TAR_BLOCK(pos);
            pkseek(pkg, pos, SEEK_SET);
        }
    }

    if (buf)
        bfree(buf);

    return code;
}

// End of tar file is signalled by return NULL with code == UPDATE_OK.
static UpdateTarHeader *parse_tar_header(UpdatePackage *pkg, UpdateCode *code) {
    struct header_gnu_tar gthdr;
    UpdateTarHeader *uthdr = NULL;
    char *ccheck;
    char **longpath;
    size_t rsize;
    long pos = pktell(pkg);
    long val = 0;
    int hdr_count = 0;
    UpdateCode rcode = UPDATE_OK;
    bool rdone = false;
    bool first_hdr_empty = false;

    uthdr = (UpdateTarHeader *)bmalloc(sizeof(UpdateTarHeader));
    if (!uthdr) {
        rcode = UPDATE_OOM;
        goto done;
    }

    // Tar headers should be on 512 byte boundaries.
    if (NOT_TAR_BLOCK(pos)) {
        pos = NEXT_TAR_BLOCK(pos);
        LOG_WARN("seeking to boundary: %ld", pos);
        pkseek(pkg, pos, SEEK_SET);
    }

    uthdr->tar_hdr_offset = (size_t)pktell(pkg);

    // Max supported tar headers: long filename, long linkname and file.
    // There can be more for meta data but we don't need support.
    while (!rdone && hdr_count++ < 3) {
        rsize = pkread(&gthdr, sizeof(struct header_gnu_tar), pkg);

        if (rsize != sizeof(struct header_gnu_tar)) {
            if ((hdr_count == 1) || (hdr_count == 2 && first_hdr_empty)) {
                // posix states this is ok but bad form.
                LOG_WARN("%d empty blocks before end of file", hdr_count - 1);
                rcode = UPDATE_OK;
                cleanup_tar_header(uthdr);
                uthdr = NULL;
            } else {
                // Not the first header this is an error.
                rcode = UPDATE_FILE_ERR;
            }

            goto done;
        }

        // Tar headers should end with two 512 byte blocks of zero bytes.
        if (empty_tar_header(&gthdr)) {
            if (hdr_count == 1) {
                // First empty block.
                first_hdr_empty = true;
                continue;
            } else if (hdr_count == 2 && first_hdr_empty) {
                // First and second empty.
                rcode = UPDATE_OK;
                cleanup_tar_header(uthdr);
                uthdr = NULL;
                goto done;
            } else {
                rcode = UPDATE_TAR_ERR;
                goto done;
            }
        }

        if (!verify_tar_header(&gthdr)) {
            rcode = UPDATE_TAR_ERR;
            goto done;
        }

        switch (gthdr.typeflag[0]) {
        case GTTYPE_HRD:
        case GTTYPE_SYM:
            if (!uthdr->linkname)
                uthdr->linkname = bstrndup(gthdr.linkname, sizeof(gthdr.linkname));
            if (!uthdr->linkname || strlen(uthdr->linkname) == 0) {
                rcode = UPDATE_TAR_ERR;
                goto done;
            }
        case GTTYPE_REG:
        case GTTYPE_REG2:
            // UpdateTarType must be inline with header_gnu_tar.typeflag.
            uthdr->type = (UpdateTarType)gthdr.typeflag[0];

            uthdr->tar_data_offset = (size_t)pktell(pkg);

            if (!uthdr->name)
                uthdr->name = bstrndup(gthdr.name, sizeof(gthdr.name));
            uthdr->uname = bstrndup(gthdr.uname, sizeof(gthdr.uname));
            uthdr->gname = bstrndup(gthdr.gname, sizeof(gthdr.gname));

            if (!uthdr->name || strlen(uthdr->name) == 0 || !uthdr->uname ||
                    !uthdr->gname) {
                rcode = UPDATE_TAR_ERR;
                goto done;
            }

            val = strtol(gthdr.mode, &ccheck, 8);
            if (*ccheck != '\0') {
                val = __LINE__;
                goto done;
            }
            uthdr->mode = (mode_t)val;

            val = strtol(gthdr.uid, &ccheck, 8);
            if (*ccheck != '\0') {
                val = __LINE__;
                goto done;
            }
            uthdr->uid = (uid_t)val;

            val = strtol(gthdr.gid, &ccheck, 8);
            if (*ccheck != '\0') {
                val = __LINE__;
                goto done;
            }
            uthdr->gid = (gid_t)val;

            val = strtol(gthdr.size, &ccheck, 8);
            if (*ccheck != '\0') {
                val = __LINE__;
                goto done;
            }
            uthdr->size = (size_t)val;

            val = strtol(gthdr.mtime, &ccheck, 8);
            if (*ccheck != '\0') {
                val = __LINE__;
                goto done;
            }
            uthdr->mtime = (time_t)val;

            val = 0;  // Important!
            rdone = true;
            break;
        case GTTYPE_LLINK:
        case GTTYPE_LPATH:
            // Set uthdr->size and uthdr->tar_data_offset to use
            // extract_tar_to_mem for extracting the long path.  The size
            // in the tar header includes the null terminator.
            uthdr->tar_data_offset = (size_t)pktell(pkg);
            val = strtol(gthdr.size, &ccheck, 8);
            if (*ccheck != '\0') {
                val = __LINE__;
                goto done;
            }
            uthdr->size = (size_t)val;
            longpath = (gthdr.typeflag[0] == GTTYPE_LLINK) ?
                    &uthdr->linkname : &uthdr->name;
            *longpath = extract_tar_to_mem(pkg, uthdr, NULL, &rcode);
            if (rcode != UPDATE_OK)
                goto done;
            break;
        default:
            LOG_ERROR("invalid type: 0x%02x", gthdr.typeflag[0]);
            rcode = UPDATE_ERR;
            goto done;
        }
    }

    // More than three headers (error should have happened before this).
    if (!rdone) {
        LOG_ERROR("expected data");
        rcode = UPDATE_TAR_ERR;
        goto done;
    }

done:
    if (val) {
        LOG_ERROR("value error at %ld", val);
        rcode = UPDATE_TAR_ERR;
    }

    if (rcode != UPDATE_OK && uthdr) {
        cleanup_tar_header(uthdr);
        uthdr = NULL;
    }

    if (code)
        *code = rcode;

    return uthdr;
}

static UpdateCode package_extract_file(UpdatePackage *pkg,
        const char *full_path, bool ramdisk) {
    FILE *stream = fopen(full_path, "w");
    UpdateCode code = UPDATE_FILE_ERR;

    if (stream) {
        code = extract_tar_to_stream(pkg, pkg->cur_uthdr, stream);
        // Set the file user, group and permissions from tar header here.
        if (code == UPDATE_OK) {
            code = (fchmod(fileno(stream), pkg->cur_uthdr->mode) == 0)
                    ? UPDATE_OK : UPDATE_FILE_ERR;
        }
        fclose(stream);
    }

    return code;
}

static UpdateCode package_extract_hardlink(UpdatePackage *pkg,
        const char *root, const char *full_path, bool ramdisk) {
    char *full_link;
    UpdateCode code = UPDATE_FILE_ERR;

    if (safe_unlink(full_path) == UPDATE_OK) {
        full_link = join_path(root, pkg->cur_uthdr->linkname);
        if (!full_link)
            return UPDATE_OOM;

        if (link(full_link, full_path) == 0) {
            code = UPDATE_OK;
        }

        bfree(full_link);
    }

    if (code == UPDATE_FILE_ERR)
        LOG_ERROR("%s", strerror(errno));

    return code;
}

static UpdateCode package_extract_symlink(UpdatePackage *pkg,
        const char *full_path, bool ramdisk) {
    if (safe_unlink(full_path) == UPDATE_OK
            && symlink(pkg->cur_uthdr->linkname, full_path) == 0) {
        return UPDATE_OK;
    }

    LOG_ERROR("%s", strerror(errno));

    return UPDATE_FILE_ERR;
}

void dump_package(UpdatePackage *pkg) {
    if (!pkg) {
        LOG_ERROR("pkg is NULL");
        return;
    }

    LOG_INFO("UpdatePackage: %p", pkg);
    LOG_INFO("  pkg->path: %s", pkg->path);
    LOG_INFO("  pkg->manifest: %p", pkg->manifest);
    LOG_INFO("  pkg->cur_uthdr: %p", pkg->cur_uthdr);
    LOG_INFO("    pkg->io.tell: %p", pkg->io.tell);
    LOG_INFO("    pkg->io.seek: %p", pkg->io.seek);
    LOG_INFO("    pkg->io.read: %p", pkg->io.read);
    LOG_INFO("    pkg->io.close: %p", pkg->io.close);
    LOG_INFO("    pkg->io.dump: %p", pkg->io.dump);
    pkg->io.dump(pkg);
    LOG_INFO("  pkg->cur_manifest_index: %d", pkg->cur_manifest_index);
    LOG_INFO("  pkg->error: %d", pkg->error);
    LOG_INFO("  pkg->compressed: %d", pkg->compressed);
    if (pkg->manifest)
        dump_manifest(pkg->manifest);
    if (pkg->cur_uthdr)
        dump_tar_header(pkg->cur_uthdr);
}

UpdatePackage *open_package(const char *path, UpdateCode *code) {
    UpdatePackage *pkg = NULL;
    UpdateTarHeader *uthdr = NULL;
    void *manifest_data;
    UpdateCode rcode = UPDATE_ERR;

    if (!path) {
        rcode = UPDATE_BAD_ARG;
        goto done;
    }

    pkg = init_package_streams(path, &rcode);
    if (!pkg)
        goto done;

    // First entry in the tar should be the package manifest.
    uthdr = parse_tar_header(pkg, &rcode);
    if (uthdr) {
        if (strcmp("manifest.yml", uthdr->name)) {
            LOG_ERROR("expected manifest");
            cleanup_tar_header(uthdr);
            goto done;
        }

        manifest_data = extract_tar_to_mem(pkg, uthdr, NULL, &rcode);
        if (manifest_data) {
            pkg->manifest = open_mem_manifest(manifest_data, uthdr->size,
                    &rcode);
            bfree(manifest_data);
            cleanup_tar_header(uthdr);
        }
    } else {
        // Covers empty (or unreadable) tar file.
        rcode = UPDATE_TAR_ERR;
    }

done:
    if (rcode != UPDATE_OK && pkg) {
        close_package(pkg);
        pkg = NULL;
    }

    if (code)
        *code = rcode;

    return pkg;
}

UpdateCode close_package(UpdatePackage *pkg) {
    if (!pkg)
        return UPDATE_BAD_ARG;

    if (pkg->io.close)
        pkg->io.close(pkg);
    if (pkg->path)
        bfree(pkg->path);
    if (pkg->manifest)
        close_manifest(pkg->manifest);
    if (pkg->cur_uthdr)
        cleanup_tar_header(pkg->cur_uthdr);
    if (pkg->last_dir)
        bfree(pkg->last_dir);
    bfree(pkg);

    return UPDATE_OK;
}

UpdateCode package_stat(UpdatePackage *pkg, UMItem **itemp) {
    UMItem *item = NULL;
    UpdateCode code = UPDATE_OK;

    if (!pkg || !itemp)
        return UPDATE_BAD_ARG;

    if (pkg->error || !pkg->manifest || pkg->manifest->count < 0) {
        code = UPDATE_INVALID_STATE;
        goto done;
    }

    // Add the end of the package.
    if (pkg->cur_manifest_index >= pkg->manifest->count) {
        goto done;
    }

    item = manifest_get(pkg->manifest, pkg->cur_manifest_index, &code);
    if (!item)
        goto done;

    // If this is not a REMOVE item check the item path with the next
    // path in the tar file.
    if (item->type != UMITEM_REMOVE) {
        // Read the next tar header if it is not current.
        if (!pkg->cur_uthdr) {
            pkg->cur_uthdr = parse_tar_header(pkg, &code);
        }

        // Check manifest and tar path are equal.
        if (code == UPDATE_OK
                && strcmp(item->path, pkg->cur_uthdr->name)) {
            LOG_ERROR("item: %s != uthdr: %s", item->path,
                    pkg->cur_uthdr->name);
            code = UPDATE_INVALID_STATE;
        }
    }

done:
    if (code != UPDATE_OK) {
        item = NULL;
        pkg->error = true;
    }

    *itemp = item;

    return code;
}

UpdateCode package_skip(UpdatePackage *pkg) {
    UMItem *item;
    UpdateCode code = package_stat(pkg, &item);
    long pos;

    // If we are at the end of the package this will return UPDATE_OK
    // but do nothing.
    if (code == UPDATE_OK && item) {
        pos = NEXT_TAR_BLOCK(pkg->cur_uthdr->tar_data_offset +
                pkg->cur_uthdr->size);
        if (pkseek(pkg, pos, SEEK_SET) != 0) {
            pkg->error = true;
            code = UPDATE_FILE_ERR;
        } else {
            pkg->cur_manifest_index++;
        }
        // Change to unparsed tar header state.
        cleanup_tar_header(pkg->cur_uthdr);
        pkg->cur_uthdr = NULL;
    }

    return code;
}

UpdateCode package_extract(UpdatePackage *pkg, const char *root, bool ramdisk) {
    UMItem *item;
    char *full_path = NULL;
    char *t;
    size_t size;
    UpdateCode code;

    if (!pkg || !root)
        return UPDATE_BAD_ARG;

    code = package_stat(pkg, &item);
    if (code != UPDATE_OK)
        return code;

    if (!item)
        return UPDATE_INVALID_STATE;

    size = strlen(item->path);
    if (item->path[size - 1] == '/') {
        LOG_ERROR("path ends with '/'");
        return UPDATE_INVALID_STATE;
    }

    full_path = join_path(root, item->path);
    if (!full_path)
        return UPDATE_OOM;

    // Switch full_path to just dir (removing last /).
    // Note: sprintf above guarantees at least one /.
    t = full_path + strlen(full_path) - 1;
    while (*t != '/' && t != full_path)
        t--;
    if (*t == '/')
        *t = '\0';

    // Make sure the directory is there.
    if (!pkg->last_dir || strcmp(pkg->last_dir, full_path)) {
        if (mkdirr(full_path, PACKAGE_MKDIR_MODE) == 0) {
            if (pkg->last_dir)
                bfree(pkg->last_dir);
            pkg->last_dir = bstrdup(full_path);
            if (!pkg->last_dir) {
                code = UPDATE_OOM;
                goto done;
            }
        }
    }

    // Switch full_path back to dir and file.
    *t = '/';

    switch (pkg->cur_uthdr->type) {
    case UTTYPE_REG: {
        code = package_extract_file(pkg, full_path, ramdisk);
        break;
    }

    case UTTYPE_HRD: {
        code = package_extract_hardlink(pkg, root, full_path, ramdisk);
        break;
    }

    case UTTYPE_SYM: {
        code = package_extract_symlink(pkg, full_path, ramdisk);
        break;
    }

    default:
        code = UPDATE_TAR_ERR;
        break;
    }

    if (code == UPDATE_OK) {
        // extract_tar_to_stream will have already set the package to the next
        // 512 byte block so change to unparsed tar header state.
        cleanup_tar_header(pkg->cur_uthdr);
        pkg->cur_uthdr = NULL;
        pkg->cur_manifest_index++;
    } else {
        pkg->error = true;
    }

done:
    if (full_path)
        bfree(full_path);

    return code;
}

UpdateCode package_remove(UpdatePackage *pkg, const char *root,
        int *remove_count) {
    UMItem *item;
    char *full_path = NULL;
    UpdateCode code = package_stat(pkg, &item);
    int count = 0;
    bool continue_on_error;

    if (code != UPDATE_OK)
        return code;

    if (!item)
        return UPDATE_INVALID_STATE;

    full_path = join_path(root, item->path);
    if (!full_path)
        return UPDATE_OOM;

    continue_on_error = (item->flags & UMITEM_FLAGS_REMOVE_CONT_ON_ERR)
            ? 1 : 0;

    if (item->flags & UMITEM_FLAGS_REMOVE_ALLOW_RECURSIVE) {
        // ALLOW_RECURSIVE implies ALLOW_DIRECTORY.  Just call
        // recursive_remove.
        code = recursive_remove(full_path, &count, continue_on_error);
    } else if (item->flags & UMITEM_FLAGS_REMOVE_ALLOW_DIRS) {
        // safe_remove will remove empty directories and files.
        code = safe_remove(full_path);
        // May not have actually removed anything if the file already
        // didn't exist.  Count it anyway.
        count = (code == UPDATE_OK) ? 1 : 0;
    } else {
        // safe_unlink will only remove files, links, etc.
        code = safe_unlink(full_path);
        // May not have actually removed anything if the file already
        // didn't exist.  Count it anyway.
        count = (code == UPDATE_OK) ? 1 : 0;
    }

    if (code != UPDATE_OK && continue_on_error) {
        LOG_INFO("ignoring error when removing %s", full_path);
        code = UPDATE_OK;
    }

    if (code == UPDATE_OK) {
        // Only update the manifest index as no corresponding file exists
        // in the tar.
        pkg->cur_manifest_index++;

        if (remove_count)
            *remove_count = count;
    } else {
        pkg->error = true;
    }

    if (full_path)
        bfree(full_path);

    return code;
}

UpdateCode package_exec(UpdatePackage *pkg, int *status) {
    UMItem *item;
    void *buf;
    UpdateCode code;
    int rstatus;

    if (!pkg)
        return UPDATE_BAD_ARG;

    code = package_stat(pkg, &item);
    if (code != UPDATE_OK)
        return code;

    // If we are at the end of the package this will return UPDATE_OK
    // but do nothing.
    if (!item)
        return code;

    switch (item->type) {
        case UMITEM_SH_SCRIPT:
        case UMITEM_LUA_SCRIPT:
            break;
        default:
            return UPDATE_BAD_ARG;
    }

    buf = extract_tar_to_mem(pkg, pkg->cur_uthdr, NULL, &code);
    if (code != UPDATE_OK) {
        pkg->error = true;
        return code;
    }

    if (item->type == UMITEM_SH_SCRIPT) {
        code = run_shell_script_mem(buf, pkg->cur_uthdr->size, &rstatus);
    } else {
        code = run_lua_script_mem(buf, pkg->cur_uthdr->size, &rstatus);
    }

    // Even if the script failed the read pointer is already at the next
    // file in the tar.
    cleanup_tar_header(pkg->cur_uthdr);
    pkg->cur_uthdr = NULL;
    pkg->cur_manifest_index++;

    bfree(buf);

    if (status)
        *status = rstatus;

    return code;
}

UpdateCode package_process(UpdatePackage *pkg, const char *root, int *status,
        bool ramdisk) {
    UMItem *item;
    int index;
    int script_count = 0;
    int file_count = 0;
    int remove_count = 0;
    int rstatus = 0;
    UpdateCode code;
    bool has_firmware = false;

    //LOG_WARN("ramdisk option does nothing");

    if (!pkg || !root)
        return UPDATE_BAD_ARG;

    // Check all of the item types before doing anything.
    for (index = 0; index < pkg->manifest->count; index++) {
        item = manifest_get(pkg->manifest, index, &code);
        if (!item)
            return code;

        switch (item->type) {
        case UMITEM_PRI_FIRMWARE:
        case UMITEM_REC_FIRMWARE:
            if (pkg->manifest->count != 1) {
                LOG_ERROR("invalid firmware package");
                return UPDATE_INVALID_STATE;
            }
            has_firmware = true;
            break;
        case UMITEM_FILE:
        case UMITEM_REMOVE:
        case UMITEM_SH_SCRIPT:
        case UMITEM_LUA_SCRIPT:
            break;
        default:
            LOG_ERROR("invalid type");
            return UPDATE_INVALID_STATE;
        }
    }

    if (has_firmware) {
        LOG_ERROR("firmware not supported");
        return UPDATE_ERR;
    }

    do {
        code = package_stat(pkg, &item);
        if (item) {
            switch (item->type) {
            case UMITEM_SH_SCRIPT:
            case UMITEM_LUA_SCRIPT:
                LOG_DEBUG("running script: %s", item->path);
                code = package_exec(pkg, &rstatus);
                if (code == UPDATE_OK)
                    script_count++;
                break;
            case UMITEM_FILE:
                LOG_DEBUG("installing file: %s", item->path);
                code = package_extract(pkg, root, ramdisk);
                if (code == UPDATE_OK)
                    file_count++;
                break;
            case UMITEM_REMOVE: {
                int last_remove_count = 0;
                LOG_DEBUG("removing: %s", item->path);
                code = package_remove(pkg, root, &last_remove_count);
                if (code == UPDATE_OK)
                    remove_count += last_remove_count;
                break;
                }
            default:
                LOG_ERROR("invalid state");
                code = UPDATE_INVALID_STATE;
            }
        }
    } while (item && code == UPDATE_OK);

    if (code == UPDATE_OK) {
        LOG_INFO("%d files installed %d removed and %d scripts run successfully",
                file_count, remove_count, script_count);
    } else {
        LOG_ERROR("failed after %d files installed %d removed and %d scripts",
                file_count, remove_count, script_count);
    }

    if (status)
        *status = rstatus;

    return code;
}
