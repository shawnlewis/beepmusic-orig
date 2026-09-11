#include <errno.h>
#include <stdlib.h>

#include <zlib.h>

#include "beepupdate.h"
#include "package_priv.h"


struct PackageIOZlib {
    gzFile gzstream;  // This is a typedef pointer.
    long size;
};

static void package_zlib_dump(UpdatePackage *pkg) {
    LOG_INFO("    pkg->io.data.zlib->gzstream: %p", pkg->io.data.zlib->gzstream);
    LOG_INFO("    pkg->io.data.zlib->size: %ld", pkg->io.data.zlib->size);
}

static long package_zlib_tell(UpdatePackage *pkg) {
    z_off_t ret = gztell(pkg->io.data.zlib->gzstream);
    if (ret < 0)
        perrno = EBADF;
    return (long)ret;
}

static int package_zlib_seek(UpdatePackage *pkg, long offset, int whence) {
    z_off_t ret;

    switch (whence) {
    case SEEK_END:
        offset = pkg->io.data.zlib->size + offset;
        break;
    case SEEK_SET:
        break;
    case SEEK_CUR:
        if (offset == 0)
            return 0;
        offset += package_zlib_tell(pkg);
        break;
    default:
        offset = -1;
        break;
    }

    if (offset > pkg->io.data.zlib->size || offset < 0) {
        perrno = EINVAL;
        return -1;
    }

    if (offset == package_zlib_tell(pkg)) {
        //LOG_DEBUG("no seek needed to: %ld", offset);
        return 0;
    }

    ret = gzseek(pkg->io.data.zlib->gzstream, offset, SEEK_SET);
    if (ret < 0)
        perrno = EBADF;
        return -1;

    return 0;
}

static size_t package_zlib_read(void *ptr, size_t size, UpdatePackage *pkg) {
    int errnum;
    const char *errstr;
    int rsize = gzread(pkg->io.data.zlib->gzstream, ptr, (unsigned)size);
    if (rsize != size) {
        // Going to treat this as an error since with a properly formatted
        // tar file we shouldn't hit EOF.
        if (gzeof(pkg->io.data.zlib->gzstream)) {
            errstr = "gzeof";
        } else {
            errstr = gzerror(pkg->io.data.zlib->gzstream, &errnum);
            if (!errstr)
                errstr = "unknown";
        }
        LOG_ERROR("read error: %s", errstr);
    }
    return (rsize < 0) ? 0 : rsize;
}

static void package_zlib_close(UpdatePackage *pkg) {
    if (pkg->io.data.zlib) {
        if (pkg->io.data.zlib->gzstream) {
            // Important to use gzclose_r as this will prevent compression
            // functions from being linked if static linking is used.
            gzclose_r(pkg->io.data.zlib->gzstream);
            pkg->io.data.zlib->gzstream = NULL;
        }
        bfree(pkg->io.data.zlib);
        pkg->io.data.zlib = NULL;
    }
}

UpdateCode package_zlib_open(UpdatePackage *pkg, FILE *stream) {
    long start_pos;
    uint32_t size;
    int fd;

    pkg->io.data.zlib = (PackageIOZlib *)bmalloc(sizeof(PackageIOZlib));
    if (!pkg->io.data.zlib)
        return UPDATE_OOM;

    start_pos = ftell(stream);
    if (start_pos < 0)
        return UPDATE_FILE_ERR;

    // There is not a way to get this from the gzFile interface and gzseek
    // with whence SEEK_END is not supported (it does work but only after
    // the gzip has been processed).  This also works since packages will
    // only ever be a single gzip stream.
    fseek(stream, -4, SEEK_END);
    if (fread(&size, 1, 4, stream) != 4)
        return UPDATE_FILE_ERR;

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    size = __builtin_bswap32(size);
#endif

    pkg->io.data.zlib->size = (long)size;
    if (pkg->io.data.zlib->size < 0)
        return UPDATE_ZLIB_ERR;

    fseek(stream, start_pos, SEEK_SET);
    // Important so FILE * pos will be updated to the fd.
    fflush(stream);

    // Luckily zlib will operate using the current position of the file
    // descriptor when gzdopen is called as the starting point of the gz
    // stream (this is done in init_package_streams).
    fd = dup(fileno(stream));
    fclose(stream);

    if (fd == -1)
        return UPDATE_FILE_ERR;

    pkg->io.data.zlib->gzstream = gzdopen(fd, "r");
    if (!pkg->io.data.zlib->gzstream)
        return UPDATE_ZLIB_ERR;

    // Set larger buffer size.
    if (gzbuffer(pkg->io.data.zlib->gzstream, FILE_CHUNK_SIZE))
        return UPDATE_ZLIB_ERR;

    pkg->io.tell = package_zlib_tell;
    pkg->io.seek = package_zlib_seek;
    pkg->io.read = package_zlib_read;
    pkg->io.close = package_zlib_close;
    pkg->io.dump = package_zlib_dump;

    return UPDATE_OK;
}
