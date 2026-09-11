#include <errno.h>

#include "beepupdate.h"
#include "package_priv.h"


struct PackageIOFile {
    FILE *stream;
    long base;
    long size;
};

static void package_file_dump(UpdatePackage *pkg) {
    LOG_INFO("    pkg->io.data.file->stream: %p", pkg->io.data.file->stream);
    LOG_INFO("    pkg->io.data.file->base: %ld", pkg->io.data.file->base);
    LOG_INFO("    pkg->io.data.file->size: %ld", pkg->io.data.file->size);
}

// Adjusts for the digest offset.
static long package_file_tell(UpdatePackage *pkg) {
    long ret = ftell(pkg->io.data.file->stream);
    if (ret < 0)
        perrno = errno;
    else
        ret -= pkg->io.data.file->base;

    return ret;
}

// pkseek does not allow the file pointer to be set beyond the end of the file.
static int package_file_seek(UpdatePackage *pkg, long offset, int whence) {
    int ret;

    switch (whence) {
    case SEEK_END:
        offset = pkg->io.data.file->size + offset;
        break;
    case SEEK_SET:
        break;
    case SEEK_CUR:
        if (offset == 0)
            return 0;
        offset += package_file_tell(pkg);
        break;
    default:
        offset = -1;
        break;
    }

    if (offset > pkg->io.data.file->size || offset < 0) {
        perrno = EINVAL;
        return -1;
    }

    if (offset == package_file_tell(pkg)) {
        //LOG_DEBUG("no seek needed");
        return 0;
    }

    ret = fseek(pkg->io.data.file->stream, offset + pkg->io.data.file->base,
            SEEK_SET);
    if (ret < 0)
        perrno = errno;

    return ret;
}

static size_t package_file_read(void *ptr, size_t size, UpdatePackage *pkg) {
    size_t rsize = fread(ptr, 1, size, pkg->io.data.file->stream);
    if (rsize != size) {
        // Going to treat this as an error since with a properly formatted
        // tar file we shouldn't hit EOF.
        LOG_ERROR("read error: %s", feof(pkg->io.data.file->stream) ? "feof" :
                ferror(pkg->io.data.file->stream) ? "ferror" : "unknown");
    }
    return rsize;
}

static void package_file_close(UpdatePackage *pkg) {
    if (pkg->io.data.file) {
        if (pkg->io.data.file->stream)
            fclose(pkg->io.data.file->stream);
        bfree(pkg->io.data.file);
        pkg->io.data.file = NULL;
    }
}

UpdateCode package_file_open(UpdatePackage *pkg, FILE *stream) {
    pkg->io.data.file = (PackageIOFile *)bmalloc(sizeof(PackageIOFile));
    if (!pkg->io.data.file)
        return UPDATE_OOM;

    pkg->io.data.file->stream = stream;
    pkg->io.data.file->base = ftell(pkg->io.data.file->stream);
    fseek(pkg->io.data.file->stream, 0, SEEK_END);
    pkg->io.data.file->size = ftell(pkg->io.data.file->stream)
            - pkg->io.data.file->base;
    fseek(pkg->io.data.file->stream, pkg->io.data.file->base, SEEK_SET);
    if (pkg->io.data.file->base < 0 || pkg->io.data.file->size < 0) {
        return UPDATE_FILE_ERR;
    }

    pkg->io.tell = package_file_tell;
    pkg->io.seek = package_file_seek;
    pkg->io.read = package_file_read;
    pkg->io.close = package_file_close;
    pkg->io.dump = package_file_dump;

    return UPDATE_OK;
}
