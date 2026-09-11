#include <string.h>

#include <uci.h>

#include "machdefs.h"
#include "beepupdate.h"

#define MAX_VERSION_STR_SIZE    1024

struct uciop {
    int offset;
    const char *path;
};

static struct uciop beep_config_paths[] = {
    {offsetof(UpdateSystemConfig, update_serv), "beep_static.main.update_serv"},
    {offsetof(UpdateSystemConfig, update_port), "beep_static.main.update_port"},
    {offsetof(UpdateSystemConfig, device_id), "beep_device.main.device_id"},
    {offsetof(UpdateSystemConfig, device_auth), "beep_device.main.device_auth"}
};

static const char *beep_sw_version_paths[] = BEEP_SW_VERSION_PATHS;


UpdateCode load_uci_config(UpdateSystemConfig *syscfg) {
    struct uci_context *ctx;
    struct uci_ptr ptr;
    char **dest;
    char path[64];
    int index;
    int ret;
    UpdateCode code = UPDATE_OK;

    ctx = uci_alloc_context();
    if (!ctx)
        return UPDATE_UCI_ERR;

    if (uci_set_confdir(ctx, UCI_CONF_DIR) != UCI_OK) {
        uci_free_context(ctx);
        return UPDATE_UCI_ERR;
    }

    for (index = 0;
            index < sizeof(beep_config_paths)/sizeof(struct uciop);
            index++) {
        // Need to copy path because uci_lookup_ptr doesn't use const
        strncpy(path, beep_config_paths[index].path, 64);
        ret = uci_lookup_ptr(ctx, &ptr, path, true);
        if (ret == UCI_OK && ptr.o && ptr.o->v.string) {
            // Address to the char * in syscfg to set for this path.
            dest = (char **)(((uint8_t *)syscfg) +
                    beep_config_paths[index].offset);
            if (*dest)
                bfree(*dest);
            *dest = bstrdup(ptr.o->v.string);
        } else {
            LOG_ERROR("%s not found", beep_config_paths[index].path);
            code = UPDATE_CONFIG_INCOMPLETE;
        }
    }

    uci_free_context(ctx);

    return code;
}

static char *load_version_file(const char *path) {
    FILE *stream = NULL;
    char *verstr = bmalloc(MAX_VERSION_STR_SIZE);
    size_t verstr_size;

    if (!verstr)
        return NULL;

    stream = fopen(path, "r");
    if (stream) {
        fread(verstr, 1, MAX_VERSION_STR_SIZE - 1, stream);
        fclose(stream);
    }

    verstr_size = strlen(verstr);

    if (!verstr_size) {
        strcpy(verstr, "unknown");
    } else if (verstr[verstr_size - 1] == '\n') {
        verstr[verstr_size - 1] = '\0';
    }

    return verstr;
}

UpdateCode load_sw_versions(UpdateSystemConfig *syscfg) {
    int index;

    // Clear current versions to refresh them.
    if (syscfg->sys_ver) {
        bfree(syscfg->sys_ver);
        syscfg->sys_ver = NULL;
    }
    if (syscfg->beep_ver) {
        bfree(syscfg->beep_ver);
        syscfg->beep_ver = NULL;
    }

#ifdef OVERRIDE_SYS_VERSION
    syscfg->sys_ver = bstrdup(OVERRIDE_SYS_VERSION);
#else
    syscfg->sys_ver = load_version_file(BEEP_SYS_VERSION_PATH);
#endif
    if (syscfg->sys_ver) {
#ifdef OVERRIDE_SW_VERSION
        syscfg->beep_ver = bstrdup(OVERRIDE_SW_VERSION);
#else
        for (index = 0; index < sizeof(beep_sw_version_paths)/sizeof(char *); index++) {
            if (syscfg->beep_ver && strcmp(syscfg->beep_ver, "unknown"))
                break;
            if (syscfg->beep_ver)
                bfree(syscfg->beep_ver);
            syscfg->beep_ver = load_version_file(beep_sw_version_paths[index]);
        }
#endif
    }
    if (!syscfg->sys_ver || !syscfg->beep_ver)
        return UPDATE_OOM;
    return UPDATE_OK;
}
