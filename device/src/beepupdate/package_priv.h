#ifndef BEEP_UPDATE_PACKAGE_PRIV_H
#define BEEP_UPDATE_PACKAGE_PRIV_H

#include "beep_tar.h"

#define FILE_CHUNK_SIZE (16 * 1024)

extern int perrno;

typedef long (*PackageTell)(UpdatePackage *pkg);
// PackageSeek does not allow the file pointer to be set beyond the end of
// the file.
typedef int (*PackageSeek)(UpdatePackage *pkg, long offset, int whence);
typedef size_t (*PackageRead)(void *ptr, size_t size, UpdatePackage *pkg);
typedef void (*PackageClose)(UpdatePackage *pkg);
typedef void (*PackageIODump)(UpdatePackage *pkg);

typedef struct PackageIOFile PackageIOFile;
typedef struct PackageIOZlib PackageIOZlib;

struct UpdatePackage {
    char *path;
    UpdateManifest *manifest;
    UpdateTarHeader *cur_uthdr;
    char *last_dir;
    struct {
        PackageTell tell;
        PackageSeek seek;
        PackageRead read;
        PackageClose close;
        PackageIODump dump;
        union {
            PackageIOFile *file;
            PackageIOZlib *zlib;
        } data;
    } io;
    int cur_manifest_index;
    bool compressed;
    bool error;
};

UpdateCode package_file_open(UpdatePackage *pkg, FILE *stream);
UpdateCode package_zlib_open(UpdatePackage *pkg, FILE *stream);

#endif  // BEEP_UPDATE_PACKAGE_PRIV_H
