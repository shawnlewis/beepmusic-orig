#include <assert.h>
#include <string.h>
#include <unistd.h>

#include "beepupdate.h"
#include "machdefs.h"

#define MAX_CHAINED_UPDATES                         (1)

static bool start_beep = false;


static UpdateCode process_uc(UpdateConfig *uc) {
    UpdatePackage *pkg;
    char *pkg_list[3] = {};
    int index;
    int status;
    UpdateCode code = UPDATE_OK;
    UpdateCode tcode;

    if (!uc)
        return UPDATE_BAD_ARG;

    LOG_DEBUG("updating to %s", uc->release);
    dump_config(uc);

    // Should we download all first then install or download/install each
    // separately?
    pkg_list[0] = server_get_file(uc, UPDATE_GET_PREINST, &code);
    pkg_list[1] = server_get_file(uc, UPDATE_GET_INSTALL, &code);
    pkg_list[2] = server_get_file(uc, UPDATE_GET_POSTINST, &code);

    if (!pkg_list[0] && !pkg_list[1] && !pkg_list[2]) {
        LOG_WARN("no packages to process");
        return UPDATE_OK;
    }

    for (index = 0;
            (index < sizeof(pkg_list)/sizeof(char *)) && code == UPDATE_OK;
            index++) {
        if (!pkg_list[index])
            continue;
        pkg = open_package(pkg_list[index], &code);
        if (pkg) {
            LOG_DEBUG("processing package type %d", index);
            code = package_process(pkg, sysconfig->prefix, &status, false);

            if (code == UPDATE_SCRIPT_NONZERO) {
                LOG_ERROR("package script failed with rc: %d", status);
                dump_package(pkg);
            } else if (code != UPDATE_OK) {
                LOG_ERROR("package_process failed with code: %d", code);
                dump_package(pkg);
            }

            tcode = close_package(pkg);
            if (code == UPDATE_OK)
                code = tcode;
        } else {
            LOG_ERROR("failed to open package with code: %d", code);
            // If we get UPDATE_DIG_FAIL here the package was corrupted or
            // started downloading and timed out.  In this case we should not
            // restart the system.
            code = UPDATE_PACKAGE_DIG_FAIL;
        }
        unlink(pkg_list[index]);
        bfree(pkg_list[index]);
        pkg_list[index] = NULL;
    }

    for (index = 0;
            (index < sizeof(pkg_list)/sizeof(char *));
            index++) {
        if (pkg_list[index]) {
            unlink(pkg_list[index]);
            bfree(pkg_list[index]);
        }
    }

    return code;
}

static UpdateCode cmd_status(void) {
    if (!sysconfig) {
        LOG_ERROR("sysconfig is NULL");
        return UPDATE_INVALID_STATE;
    }

    LOG_INFO("update_serv: %s", sysconfig->update_serv);
    LOG_INFO("update_port: %s", sysconfig->update_port);
    LOG_INFO("device_id: %s", sysconfig->device_id);
    LOG_INFO("sys_ver: %s", sysconfig->sys_ver);
    LOG_INFO("beep_ver: %s", sysconfig->beep_ver);
    LOG_INFO("requested: %s", sysconfig->requested);
    LOG_INFO("prefix: %s", sysconfig->prefix);
    LOG_INFO("check_unless_force: %d", sysconfig->check_unless_force);
    LOG_INFO("state: 0x%08x", sysconfig->state);

    return UPDATE_OK;
}

static UpdateCode cmd_update(void) {
    UpdateConfig *uc;
    UpdateCode code;
    int counter = MAX_CHAINED_UPDATES;

    uc = server_get_config(sysconfig->update_serv, sysconfig->update_port,
            &code);
    // Clear the update ready file if already up to date.
    if (code == UPDATE_UP_TO_DATE) {
        safe_unlink(UPDATE_READY_PATH);
    }
    // This happens for error or device up to date.
    if (!uc)
        return code;

    // Update is ready, we're just checking and not forced by the server
    // touch the update ready file.
    if (sysconfig->check_unless_force && !uc->force) {
        LOG_DEBUG("deferring update to %s", uc->release);
        code = ftouch(UPDATE_READY_PATH);
        if (code != UPDATE_OK)
            LOG_ERROR("could not touch update ready file");
        // We're done either way.
        cleanup_config(uc);
        return code;
    }

    // Shutdown beep services if they are running and set if they should
    // be started again before exiting.
    code = stop_beep_services();
    if (code == UPDATE_OK) {
        start_beep = true;
    } else if (code != UPDATE_NOT_FOUND) {
        // Not found means beep services were not running.  Anything else
        // is an error.
        cleanup_config(uc);
        return code;
    }

    code = UPDATE_OK;

    // Loop updates until we hit an error the server reports back up to date.
    while (uc && code == UPDATE_OK && counter--) {
        code = process_uc(uc);

        // process_uc will return UPDATE_OK (with a warning) if there were no
        // packages downloaded.  This would be from a bad update config or if
        // the download timed out.  If we do get an error back it means there
        // was a problem actually processing the package and we should reboot.
        if (code != UPDATE_OK && code != UPDATE_PACKAGE_DIG_FAIL) {
            LOG_ERROR("process_uc failed with code %d", code);
            system_reset();
        }

        cleanup_config(uc);
        uc = NULL;

        // Refresh the software versions.
        if (code == UPDATE_OK) {
            code = load_sw_versions(sysconfig);
        }

        if (code == UPDATE_OK) {
            uc = server_get_config(sysconfig->update_serv,
                    sysconfig->update_port, &code);
        }
    }

    if (uc)
        cleanup_config(uc);

    if (counter == -1) {
        LOG_ERROR("max updates hit");
        // Let beepmanager know there are still updates waiting
        // although this is really an error case.
        ftouch(UPDATE_READY_PATH);
    }

    // Clear the update ready file.
    if (code == UPDATE_UP_TO_DATE) {
        safe_unlink(UPDATE_READY_PATH);
    }

    return code;
}

static UpdateCode cmd_boot_action(void) {
    // Later do boot checks like verify the file system and compare
    // local configs with beep config space.  Right now just update
    // and always start beep services.
    start_beep = true;
    return cmd_update();
}

static UpdateCode cmd_beep_start(void) {
    start_beep = true;
    return UPDATE_OK;
}

static UpdateCode cmd_beep_stop(void) {
    return stop_beep_services();
}

static UpdateCode cmd_bootcount(void) {
    return clear_bootcount();
}

static UpdateCode cmd_reset_bootcount(void) {
    return reset_bootcount();
}

int main(int argc, char **argv) {
    UpdateCode code = UPDATE_ERR;

    config_init(argc, argv);
    libraries_init();

    switch (sysconfig->cmd) {
    case UPDATE_CMD_STATUS:
        code = cmd_status();
        break;
    case UPDATE_CMD_UPDATE:
        code = cmd_update();
        break;
    case UPDATE_CMD_BOOT_ACTION:
        code = cmd_boot_action();
        break;
    case UPDATE_CMD_BEEP_STOP:
        code = cmd_beep_stop();
        break;
    case UPDATE_CMD_BEEP_START:
        code = cmd_beep_start();
        break;
    case UPDATE_CMD_BOOTCOUNT:
        code = cmd_bootcount();
        break;
    case UPDATE_CMD_RESET_BOOTCOUNT:
        code = cmd_reset_bootcount();
        break;
    default:
        LOG_ERROR("unknown command");
    }

    LOG_INFO("cmd: %d completed with code: %d", sysconfig->cmd, code);

    libraries_cleanup();
    system_config_cleanup();
    sysconfig = NULL;

    if (start_beep)
        start_beep_services();

    update_log_cleanup();

    return 0;
}
