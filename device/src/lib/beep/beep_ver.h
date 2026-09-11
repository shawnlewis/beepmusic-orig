#ifndef BEEP_VERSION_H
#define BEEP_VERSION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BEEP_VERSION_TIP_VER                        (2)

#define BEEP_VERSION_OPENWRT                        (1<<0)
#define BEEP_VERSION_GIT_CLEAN                      (1<<1)
#define BEEP_VERSION_GIT_DIRTY                      (2<<1)

#define BEEP_VERSION_BUILDER_SIZE                   (32)
#define BEEP_VERSION_DATE_SIZE                      (32)
#define BEEP_VERSION_GCC_VER_SIZE                   (32)
#define BEEP_VERSION_GIT_REV_SIZE                   (40)

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define BEEP_VERSION_BIG_ENDIAN_32(val) \
        ((((uint32_t)((val) & 0x000000ff)) << 24) | \
        (((uint32_t)((val) & 0x0000ff00)) << 8) | \
        (((uint32_t)((val) & 0x00ff0000)) >> 8) | \
        (((uint32_t)((val) & 0xff000000)) >> 24))
#elif __BYTE_ORDER__ == __ORDER_PDP_ENDIAN__
#define BEEP_VERSION_BIG_ENDIAN_32(val) \
        ((((uint32_t)((val) & 0x000000ff)) << 8) | \
        (((uint32_t)((val) & 0x0000ff00)) >> 8) | \
        (((uint32_t)((val) & 0x00ff0000)) << 8) | \
        (((uint32_t)((val) & 0xff000000)) >> 8))
#else  // __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define BEEP_VERSION_BIG_ENDIAN_32(val) (val)
#endif  // __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

typedef struct {
    uint32_t tip_ver;
    uint32_t flags;
    const char builder[BEEP_VERSION_BUILDER_SIZE + 1];
    const char date[BEEP_VERSION_DATE_SIZE + 1];
    const char gcc_ver[BEEP_VERSION_GCC_VER_SIZE + 1];
    const char git_rev[BEEP_VERSION_GIT_REV_SIZE + 1];
} __attribute__ ((packed)) BeepVersion;

extern const BeepVersion beep_version;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif
