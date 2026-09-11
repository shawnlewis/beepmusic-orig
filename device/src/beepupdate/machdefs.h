#ifndef BEEP_UPDATE_MACH_DEFS_H
#define BEEP_UPDATE_MACH_DEFS_H


#if BEEP_DEVICE

#define BEEP_SW_VERSION_PATHS           {"/beep/platform/VERSION"}
#define BEEP_SYS_VERSION_PATH           "/beep/SYSVER"
//#define BMEM_COUNTER
//#define OVERRIDE_SW_VERSION Don't use
//#define OVERRIDE_SYS_VERSION Don't use
#define START_BEEP_SERVICES_ARGV        {"/beep/beepio", NULL}
#define UCI_CONF_DIR                    "/etc/config"
#define UPDATE_DEFAULT_INST_PREFIX      "/"
#define DEFAULT_SYS_STATE               (STATE_PRIMARY_PART | STATE_NETWORK_READY | STATE_FS_READY | STATE_SYSLOG_READY)
#define BOOTCOUNT_DEV                   "/dev/mtd1"
#define BOOTCOUNT_IN_MTD
#define UPDATE_READY_PATH               "/tmp/__UPDATE_READY"

#elif BEEP_VIRTUAL  // BEEP_DEVICE

#define BEEP_SW_VERSION_PATHS           {"./beepupdate_config/VERSION", "./beepupdate_config/ALTVERSION"}
#define BEEP_SYS_VERSION_PATH           "./beepupdate_config/SYSVER"
#define BMEM_COUNTER
//#define OVERRIDE_SW_VERSION             "dev"
#define OVERRIDE_SYS_VERSION            "virtual"
#define START_BEEP_SERVICES_ARGV        {"/bin/sleep", "15", NULL}
#define UCI_CONF_DIR                    "./beepupdate_config"
#define UPDATE_DEFAULT_INST_PREFIX      "./beepupdate_instroot"
#define DEFAULT_SYS_STATE               (STATE_PRIMARY_PART | STATE_NETWORK_READY | STATE_FS_READY)
#define BOOTCOUNT_DEV                   "./beepupdate_config/fakemtd1"
#define UPDATE_READY_PATH               "./beepupdate_config/__UPDATE_READY"

#else  // BEEP_VIRTUAL
#error "Unknown machdefs"
#endif

#endif  // BEEP_UPDATE_MACH_DEFS_H
