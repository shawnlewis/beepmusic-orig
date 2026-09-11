#ifndef BEEP_UPDATE_TAR_H
#define BEEP_UPDATE_TAR_H

#include <sys/stat.h>

// GNU tar format is the default used by tar.

// Notes:
// - Numerical values are stored in ascii encoded octal with leading zeros and
//   null terminated.
// - Checksum is calculated by setting all bytes in the checksum field to
//   spaces then adding each byte using unsigned arithmetic.  The value is
//   stored in checksum as ascii encoded octal terminated with a null and a
//   space.
// - Data and header boundaries are every 512 bytes.
// - Unused data should be set to NULL.
// - Names up to 100 bytes can be stored in the name field.  Names longer use
//   two tar headers.  The first contains the long file name as data.  The
//   values for this header are as follows:
//     name: "././@LongLink"
//     mode, uid, gui, mtime: "0000000"
//     typeflag: 'L'
//     uname, gname: "root"
//   The beginning of the name up to 100 bytes is stored in the next header
//   along with the normal information.  512 byte boundaries are still used.
// - Links use the linkname to show where the link is pointing and pathname
//   as the link location.  Links over 100 bytes that point to files over 100
//   bytes will use three tar headers designated with the L and K typeflags.
// - Tar files always seem to be padded to a multiple of 10240 bytes, although
//   there is no strict requirement for this.

struct header_gnu_tar {
    char name[100];                 // Path of file
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag[1];               // Type of entry
    char linkname[100];             // Link pointed to by name
    char magic[6];                  // Magic ("ustar ")
    char version[2];                // Version (" \0" or {0x20, 0x00})
    char uname[32];                 // Ascii uname (preferred over uid)
    char gname[32];                 // Ascii gname (preferred over gid)
    char devmajor[8];               // Major number for chr/blk devices
    char devminor[8];               // Minor number for chr/blk devices
    char atime[12];
    char ctime[12];
    char offset[12];
    char longnames[4];
    char unused[1];
    struct {
        char offset[12];
        char numbytes[12];
    } sparse[4];
    char isextended[1];
    char realsize[12];
    char pad[17];
};

#define GTTYPE_REG      '0'
#define GTTYPE_REG2     '\0'
#define GTTYPE_HRD      '1'
#define GTTYPE_SYM      '2'
#define GTTYPE_CHR      '3'
#define GTTYPE_BLK      '4'
#define GTTYPE_DIR      '5'
#define GTTYPE_FIFO     '6'
#define GTTYPE_CONT     '7'
#define GTTYPE_DIREN    'D'
#define GTTYPE_LLINK    'K'         // Long linkname for following entry
#define GTTYPE_LPATH    'L'         // Lone pathname for following entry
#define GTTYPE_LASTF    'M'         // Continuation of last file (used for multi volume)
#define GTTYPE_SPRS     'S'         // Sparse regular file
#define GTTYPE_TAPE     'V'         // Tape/volume name

#define GTMAGIC         "ustar "
#define GTMAGIC_SIZE    6           // Does not include trailing '\0'
#define GTVER           " \0"
#define GTVER_SIZE      2           // Does not include trailing '\0'


// Only need a subset of types external to tar processing.
typedef enum {
    UTTYPE_INVALID = 0,  // Must be zero.
    UTTYPE_REG = GTTYPE_REG,
    UTTYPE_HRD = GTTYPE_HRD,
    UTTYPE_SYM = GTTYPE_SYM
} UpdateTarType;

// Easier to use structure for tar headers.
typedef struct {
    char *name;
    char *linkname;
    char *uname;
    char *gname;
    mode_t mode;
    uid_t uid;
    gid_t gid;
    size_t size;  // This should be off_t but tar headers can't hold it.
    time_t mtime;
    UpdateTarType type;
    size_t tar_hdr_offset;
    size_t tar_data_offset;
} UpdateTarHeader;

#define IS_TAR_BLOCK(__VAL__) \
    (((__VAL__) & 0x1ff) == 0)

#define NOT_TAR_BLOCK(__VAL__) \
    ((__VAL__) & 0x1ff)

#define NEXT_TAR_BLOCK(__VAL__) \
    (((__VAL__) + 0x1ff) & ~0x1ff)

#define TO_NEXT_TAR_BLOCK(__VAL__) \
    (0x200 - ((__VAL__) & 0x1ff))

#endif  // BEEP_UPDATE_TAR_H
