#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <mtd/mtd-user.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/klog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "machdefs.h"
#include "beepupdate.h"


#define FIND_PID_BUFFER_SIZE                        (1024)
#define BEEPMANAGER_PROCESS_NAME                    "beepmanagerd"
#define BEEPIO_PROCESS_NAME                         "beepio"
#define SIGTERM_USEC_DELAY                          (10000)
#define SIGTERM_RETRIES                             (100)

#define SYSLOG_ACTION_SIZE_BUFFER                   (10)
#define SYSLOG_ACTION_READ_ALL                      (3)

// Logging may not be available in this function.
void update_system_state(void) {
    sysconfig->state = DEFAULT_SYS_STATE;
}

// Only returns first found.
static UpdateCode find_pid(const char *pname, pid_t *pid, pid_t *pgid) {
    DIR *dirp = NULL;
    struct dirent *entry = NULL;
    FILE *stream = NULL;
    char *pbuf = NULL;
    char *comm_buf;
    char *ccheck;
    size_t comm_size;
    long pid_max;
    long pval;
    pid_t rpid = 0;
    pid_t rpgid = 0;
    // pid_t should be int but just to be safe for fscanf.
    int rpid_int;
    int rpgid_int;
    int fret;
    UpdateCode code = UPDATE_NOT_FOUND;

    if (!pname)
        return UPDATE_BAD_ARG;

    pbuf = (char *)bmalloc(FIND_PID_BUFFER_SIZE);
    if (!pbuf)
        return UPDATE_OOM;

    stream = fopen("/proc/sys/kernel/pid_max", "r");
    if (!stream) {
        code = UPDATE_FILE_ERR;
        goto done;
    }

    if (fread(pbuf, 1, FIND_PID_BUFFER_SIZE - 1, stream) == 0) {
        code = UPDATE_FILE_ERR;
        goto done;
    }
    fclose(stream);
    stream = NULL;

    pid_max = strtol(pbuf, &ccheck, 10);
    if ((*ccheck != '\0' && *ccheck != '\n') || pid_max <= 0) {
        code = UPDATE_INVALID_STATE;
        goto done;
    }

    dirp = opendir("/proc");
    if (!dirp) {
        code = UPDATE_FILE_ERR;
        goto done;
    }

    while ((entry = readdir(dirp)) != NULL && code != UPDATE_OK) {
        pval = strtol(entry->d_name, &ccheck, 10);
        if (*ccheck != '\0') {
            continue;
        }
        if (pval <= 0 || pval > pid_max) {
            LOG_WARN("ignoring %ld", pval);
            continue;
        }

        if (snprintf(pbuf, FIND_PID_BUFFER_SIZE, "/proc/%s/stat",
                entry->d_name) <= 0) {
            code = UPDATE_ERR;
            goto done;
        }

        stream = fopen(pbuf, "r");
        if (!stream) {
            // Either permission denied or the process went away.
            continue;
        }
        comm_buf = NULL;
        fret = fscanf(stream, "%d %ms %*c %*d %d", &rpid_int, &comm_buf, &rpgid_int);
        fclose(stream);
        stream = NULL;
        if (fret != 3) {
            if (comm_buf) {
                free(comm_buf);
            }
            LOG_ERROR("reading pid %ld stat", pval);
            continue;
        }
        comm_size = strlen(comm_buf);
        if (comm_buf[0] == '(' && comm_buf[comm_size - 1] == ')') {
            // Remove trailing ')' and compare after leading '('.
            comm_buf[comm_size - 1] = '\0';
            //LOG_DEBUG("process: %s pid: %d pgid: %d", comm_buf + 1, rpid_int, rpgid_int);
            if (!strcmp(comm_buf + 1, pname)) {
                //LOG_INFO("match found");
                rpid = (pid_t)rpid_int;
                rpgid = (pid_t)rpgid_int;
                code = UPDATE_OK;
            }
        }
        free(comm_buf);
    }

done:
    if (dirp)
        closedir(dirp);

    if (stream)
        fclose(stream);

    if (pbuf)
        bfree(pbuf);

    if (code == UPDATE_OK) {
        if (pid)
            *pid = rpid;
        if (pgid)
            *pgid = rpgid;
    }

    return code;
}

static UpdateCode kill_group_by_name(const char *pname) {
    pid_t pid[2] = {};
    pid_t pgid[2] = {};
    int ret;
    int count = 0;
    UpdateCode code;

    code = find_pid(pname, &pid[0], &pgid[0]);
    LOG_DEBUG("code: %d pid: %d pgid: %d", code, pid[0], pgid[0]);
    if (code != UPDATE_OK) {
        return code;
    }
    if (pgid[0] == 1) {
        LOG_ERROR("invalid pid");
        return UPDATE_ERR;
    }
    ret = kill(pgid[0] * -1, SIGTERM);
    if (ret != 0) {
        LOG_ERROR("SIGTERM failed with %s", strerror(errno));
        return UPDATE_ERR;
    }
    // See if process terminates and check matching pids.
    while ((code = find_pid(pname, &pid[1], &pgid[1])) == UPDATE_OK
            && pid[0] == pid[1]
            && pgid[0] == pgid[1]
            && count++ < SIGTERM_RETRIES) {
        usleep(SIGTERM_USEC_DELAY);
    }
    // process is still alive.
    if (count > SIGTERM_RETRIES) {
        LOG_WARN("sending SIGKILL for %s", pname);
        // Going to assume SIGKILL will work if ret == 0, don't check again.
        ret = kill(pgid[0] * -1, SIGKILL);
        if (ret != 0) {
            LOG_ERROR("SIGKILL failed with %s", strerror(errno));
            return UPDATE_ERR;
        }
    }

    return UPDATE_OK;
}

UpdateCode stop_beep_services(void) {
    UpdateCode rcode = kill_group_by_name(BEEPMANAGER_PROCESS_NAME);
    if (rcode == UPDATE_OK || rcode == UPDATE_NOT_FOUND) {
        return kill_group_by_name(BEEPIO_PROCESS_NAME);
    }
    return rcode;
}

// The options during failure are to reset the system via writing to
// /proc/sysrq-trigger or provide some notification via LEDs that the system
// needs to be restarted.
static void sbs_exit(int status) {
    update_log_cleanup();
    exit(status);
}

static void sbs_sig_handler(int sig) {
    switch(sig) {
        case SIGALRM:
        case SIGCHLD:
            sbs_exit(2);
            break;
        case SIGUSR1:
            sbs_exit(0);
            break;
    }
}

void start_beep_services(void) {
    char *argv[] = START_BEEP_SERVICES_ARGV;
    pid_t pid;
    pid_t ppid;
    pid_t sid;

    LOG_DEBUG("start");

    // If this was run from init skip the forking.
    if (getppid() != 1) {
        signal(SIGALRM, sbs_sig_handler);
        signal(SIGCHLD, sbs_sig_handler);
        // Use this to notify that the child was successful.
        signal(SIGUSR1, sbs_sig_handler);

        pid = fork();
        if (pid < 0) {
            LOG_ERROR("fork failed: %s", strerror(errno));
            sbs_exit(2);
        } else if (pid > 0) {
            // Wait for notification from child and exit accordingly.
            alarm(2);
            pause();
            sbs_exit(2);
        }

        ppid = getppid();

        // Ignore signals which will cause the child to exit.
        signal(SIGCHLD, SIG_DFL);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGHUP, SIG_IGN);
        signal(SIGTERM, SIG_DFL);

        // Change security and uid/gid stuff goes here.  This will let
        // us drop the beep services to a less privileged user later.
        // See getpwnam.
        umask(0);

        sid = setsid();
        if (sid < 0) {
            LOG_ERROR("create sid failed: %s", strerror(errno));
            sbs_exit(2);
        }

        // Changing directory prevents any unwanted locks for other updates
        // although this should get killed before another beepupdate starts
        // to overwrite files.
        if (chdir("/") < 0) {
            LOG_ERROR("chdir failed: %s", strerror(errno));
            sbs_exit(2);
        }

        // Make everything be quite.
        freopen("/dev/null", "r", stdin);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);

        LOG_DEBUG("starting beep services");
        // Notify parent to shutdown.
        kill(ppid, SIGUSR1);
    } else {
        // Different lineno in logs will notify if this was run from init
        // or somewhere else.
        LOG_DEBUG("starting beep services");
        update_log_cleanup();
    }

    // Place holder for now but this can call beepmanagerd.sh
    execv(argv[0], argv);

    // Beepupdate logging is no longer available now.  If execv failes
    // abort will send a crash dump.
    // Note: we can add check to the crash reporter that if the dump was
    // caused by beepupdate restart the system.
    abort();
}

static UpdateCode _reset_bootcount(int fd) {
#ifdef BOOTCOUNT_IN_MTD
    struct erase_info_user eiu = {0, BOOTCOUNT_OFFSET + BOOTCOUNT_SIZE};
#endif  // BOOTCOUNT_IN_MTD
    void *buf = NULL;
    ssize_t rwsize;
    UpdateCode rcode = UPDATE_ERR;

#ifdef BOOTCOUNT_IN_MTD
    buf = bmalloc(BOOTCOUNT_OFFSET);
#else  // BOOTCOUNT_IN_MTD
    buf = bmalloc(BOOTCOUNT_OFFSET + BOOTCOUNT_SIZE);
    // Needed to simulate a mtd erase.
    memset(buf, 0xff, BOOTCOUNT_OFFSET + BOOTCOUNT_SIZE);
#endif  // BOOTCOUNT_IN_MTD
    if (!buf) {
        return UPDATE_OOM;
    }

    // Ensure reading from the beginning of the sector.
    if (lseek(fd, 0, SEEK_SET)) {
        LOG_ERROR("could not seek to beginning");
        goto done;
    }

    rwsize = read(fd, buf, BOOTCOUNT_OFFSET);
    if (rwsize != BOOTCOUNT_OFFSET) {
        LOG_ERROR("could not read env data from sector");
        goto done;
    }
    if (lseek(fd, 0, SEEK_SET)) {
        LOG_ERROR("could not seek to beginning");
        goto done;
    }

#ifdef BOOTCOUNT_IN_MTD
    ioctl(fd, MEMUNLOCK, &eiu);
    if (ioctl(fd, MEMERASE, &eiu) == 0) {
        rwsize = write(fd, buf, BOOTCOUNT_OFFSET);
    } else {
        LOG_ERROR("erasing sector");
        goto done;
    }
    ioctl(fd, MEMLOCK, &eiu);
#else  // BOOTCOUNT_IN_MTD
    rwsize = write(fd, buf, BOOTCOUNT_OFFSET + BOOTCOUNT_SIZE);
#endif  // BOOTCOUNT_IN_MTD

#ifdef BOOTCOUNT_IN_MTD
    if (rwsize != BOOTCOUNT_OFFSET) {
#else  // BOOTCOUNT_IN_MTD
    if (rwsize != BOOTCOUNT_OFFSET + BOOTCOUNT_SIZE) {
#endif  // BOOTCOUNT_IN_MTD
        LOG_ERROR("writing env data");
    } else {
        rcode = UPDATE_OK;
    }

done:
    if (buf)
        bfree(buf);

    return rcode;
}

UpdateCode clear_bootcount(void) {
#ifdef BOOTCOUNT_IN_MTD
    struct erase_info_user eiu = {0, BOOTCOUNT_OFFSET + BOOTCOUNT_SIZE};
#endif  // BOOTCOUNT_IN_MTD
    off_t offset;
    ssize_t rwsize;
    uint32_t data = 0;
    UpdateCode rcode = UPDATE_ERR;
    int fd = -1;
    int i;
    uint8_t byte;
    uint8_t nibble;

    fd = open(BOOTCOUNT_DEV, O_RDWR);
    if (fd == -1) {
        LOG_ERROR("Could not open %s: %s", BOOTCOUNT_DEV, strerror(errno));
        goto done;
    }

    offset = lseek(fd, BOOTCOUNT_OFFSET, SEEK_SET);
    if (offset != BOOTCOUNT_OFFSET) {
        LOG_ERROR("Could not seek to offset: %d got %jd",
                BOOTCOUNT_OFFSET, (intmax_t)offset);
        goto done;
    }

    while(!data) {
        rwsize = read(fd, &data, sizeof(uint32_t));
        if (rwsize != sizeof(uint32_t)) {
            offset = lseek(fd, 0, SEEK_CUR);
            if (offset == (BOOTCOUNT_OFFSET + BOOTCOUNT_SIZE)) {
                // This is actually an error as we should have never ended up
                // with a fully depleted bootcount area.  It should have been
                // reset as the last nibble was consumed.  Reset the bootcount
                // area and continue as everything is normal.
                LOG_ERROR("bootcount area depleted");
                rcode = _reset_bootcount(fd);
                goto done;
            } else {
                printf("Error reading device: %s", strerror(errno));
                goto done;
            }
        }
    }

    // Get the first non-zero byte.
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    data = __builtin_bswap32(data);
#endif
    for (i = 24; i >= 0; i -= 8) {
        byte = data >> i;
        if (byte)
            break;
    }

    // Get the first non-zero nibble.
    nibble = ((byte & 0xf0) != 0) ? byte >> 4 : byte;
    //LOG_DEBUG("data: %08x i: %d byte: %02x nibble: %02x", data, i, byte,
    //        nibble);

    switch (nibble) {
    case 0x0f:
        // Already reset, issue a warning because something went wrong, but
        // return ok.
        LOG_WARN("nibble is in reset state");
        rcode = UPDATE_OK;
        goto done;
        break;

    case 0x07:
        // Normal we only had one boot.
        break;

    case 0x03:
    case 0x01:
        LOG_WARN("%d attempted boots before clearing bootcount",
                (nibble == 0x03) ? 1 : 2);
        break;

    default:
        LOG_ERROR("invalid bootcount value: 0x%02x", nibble);
        break;
    }

    // Clear the correct nibble.
    byte = ((byte & 0xf0) != 0) ? 0x0f : 0x00;
    // Calculate how far back we need to seek to get to the right byte.
    offset = -1 - (i / 8);

    //data = (data & ~(0xff << i)) | (byte << i);
    //LOG_DEBUG("b: %02x data: %08x\n", byte, data);
    //LOG_DEBUG("offset: %jd\n", (intmax_t)offset);

    lseek(fd, offset, SEEK_CUR);

    offset = lseek(fd, 0, SEEK_CUR);
    //LOG_DEBUG("now at: %jd\n", (intmax_t)offset);

    // At the end of the last byte, reset the bootcount area.
    if (offset == BOOTCOUNT_OFFSET + BOOTCOUNT_SIZE - 1) {
        LOG_DEBUG("at last byte reseting bootcount");
        rcode = _reset_bootcount(fd);
        goto done;
    }

#ifdef BOOTCOUNT_IN_MTD
    ioctl(fd, MEMUNLOCK, &eiu);
#endif  // BOOTCOUNT_IN_MTD
    rwsize = write(fd, &byte, 1);
#ifdef BOOTCOUNT_IN_MTD
    ioctl(fd, MEMLOCK, &eiu);
#endif  // BOOTCOUNT_IN_MTD

    // WAR for unknown kernel or 9331 bug:
    // Writing a single byte to NOR flash does not seem to trigger a
    // flush of the written byte until previously unread data has been
    // read.  Since we seeked to the start of the bootcount area reading
    // byte zero will fix this.  sync() fsync(fd) did not work.
    // To see this bug:
    // 1) Comment out the next two lines, uncomment some debug lines above.
    // 2) beepupdate bc (find the 07 or 7f byte being read out).
    // 3) beepupdate bc (same).
    // 4) dd if=/dev/mtd1 bs=1 count=1 seek=offset | hexdump (same byte).
    // 5) beepupdate bc (same).
    // 6) dd if=/dev/mtd1 | hexdump (data wll be updated).
    // 7) beepupdate bc (get warning about bootcount already reset).
    lseek(fd, 0, SEEK_SET);
    read(fd, &byte, 1);

    if (rwsize != 1) {
        LOG_ERROR("could not clear bootcount at %jd", (intmax_t)offset);
    } else {
        rcode = UPDATE_OK;
    }

done:
    if (fd != -1) {
        close(fd);
    }

    return rcode;
}

UpdateCode reset_bootcount(void) {
    int fd = open(BOOTCOUNT_DEV, O_RDWR);
    UpdateCode rcode;
    if (fd == -1) {
        LOG_ERROR("Could not open %s: %s", BOOTCOUNT_DEV, strerror(errno));
        return UPDATE_ERR;
    }
    rcode = _reset_bootcount(fd);
    close(fd);
    return rcode;
}

static int find_in_kmsg(const char *msg) __attribute__((__unused__));
static int find_in_kmsg(const char *msg) {
    char *buf;
    char *ret;
    int len = klogctl(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0);

    if (len <= 0) {
        return 0;
    }

    buf = malloc(len + 1);
    if (!buf) {
        return 0;
    }
    // Ensure we will hit a NULL char even if the buffer has not been filled.
    memset(buf, 0, len + 1);

    len = klogctl(SYSLOG_ACTION_READ_ALL, buf, len);

    ret = strstr(buf, msg);

    bfree(buf);

    return ret ? 1 : 0;
}

void system_reset(void) {
#if BEEP_DEVICE
    FILE *stream;
    int timeout;
#endif  // BEEP_DEVICE

    LOG_ERROR("system restarting");

    // Don't do this on the virtual machines.
#if BEEP_DEVICE
    stream = fopen("/proc/sysrq-trigger", "a");
    if (!stream) {
        abort();
    }

    // Ignore TERM since we're about to send SIGTERM to all processes.
    signal(SIGTERM, SIG_IGN);

    // Send SIGTERM to all processes.
    fputs("e\n", stream);
    fflush(stream);
    usleep(2000000);

    // Flush data to disk.
    fputs("s\n", stream);
    fflush(stream);
    timeout = 20;
    while(timeout--) {
        if (find_in_kmsg("Emergency Sync complete"))
            break;
        usleep(100000);
    }

    if (timeout == -1) {
        LOG_ERROR("sync timed out");
    }

    // Remount in R/O.
    fputs("u\n", stream);
    fflush(stream);
    timeout = 20;
    while(timeout--) {
        if (find_in_kmsg("Emergency Remount complete"))
            break;
        usleep(100000);
    }

    if (timeout == -1) {
        LOG_ERROR("remount timed out");
    }

    // Reboot.
    fputs("b\n", stream);
    fflush(stream);
#endif  // BEEP_DEVICE

    abort();
}
