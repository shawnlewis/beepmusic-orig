#include "beep/beep_ver.h"

const BeepVersion beep_version __attribute__ ((visibility ("hidden")))
        __attribute__ ((section (".beep"))) = {
    .tip_ver = BEEP_VERSION_BIG_ENDIAN_32(BEEP_VERSION_TIP_VER),
    .flags = BEEP_VERSION_BIG_ENDIAN_32(
#ifdef VER_SET_OPENWRT
        BEEP_VERSION_OPENWRT |
#endif
#ifdef VER_SET_GIT_CLEAN
        BEEP_VERSION_GIT_CLEAN |
#endif
#ifdef VER_SET_GIT_DIRTY
        BEEP_VERSION_GIT_DIRTY |
#endif
        0),
    .builder =
#ifdef VER_SET_BUILDER
        VER_SET_BUILDER
#else
        "unknown"
#endif
        ,
    .date = __DATE__ " " __TIME__,
    .gcc_ver = __VERSION__,
    .git_rev =
#ifdef VER_SET_GIT_REV
        VER_SET_GIT_REV
#else
        "0000000000000000000000000000000000000000"
#endif
};

