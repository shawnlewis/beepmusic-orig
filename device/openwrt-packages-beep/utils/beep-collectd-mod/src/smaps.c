#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "collectd.h"
#include "common.h"
#include "plugin.h"

struct smaps_info {
    uint64_t rss;
    uint64_t pss;
    uint64_t uss;
};

struct proc_entry;

struct proc_entry {
    char *name;
    struct proc_entry *next;
};


static struct proc_entry *proc_head;
static int proc_count;

static int proc_add(const char *name) {
    struct proc_entry *new = (struct proc_entry *)
            malloc(sizeof(struct proc_entry));

    if (!new)
        return 1;

    new->name = strdup(name);
    if (!new->name) {
        free(new);
        return 1;
    }

    new->next = proc_head;
    proc_head = new;
    proc_count++;

    return 0;
}

static void proc_clear(void) {
    while (proc_head) {
        struct proc_entry *p = proc_head;
        proc_head = proc_head->next;
        if (p->name)
            free(p->name);
        free(p);
    }
}

// must free return value, must be called after all entries added.
static void *hist_init(void) {
    if (!proc_count)
        return NULL;

    return calloc(proc_count, sizeof(int));
}

static int proc_find(const char *name, void *hist) {
    int *matched = (int *)hist;
    struct proc_entry *p = proc_head;

    if (!proc_count || !hist)
        return 0;

    while (p) {
        if (!(*matched) && !strcmp(p->name, name)) {
            *matched = 1;
            return 1;
        }
        matched++;
        p = p->next;
    }
    return 0;
}

static const char *smaps_keys[] = {
    "watch",
    NULL
};
static int smaps_keys_count = 1;

static int smaps_config(const char *key, const char *val) {
    if (!strcmp(key, "watch")) {
        proc_add(val);
    } else {
        return 1;
    }
    return 0;
}

static int smaps_shutdown(void) {
    proc_clear();
    return 0;
}

static void _smaps_submit(const char *plugin_instance,
        const char *type_instance, uint64_t type_value) {
    value_t values[1];
    value_list_t vl = VALUE_LIST_INIT;

    values[0].gauge = type_value;
    vl.values = values;
    vl.values_len = 1;

    sstrncpy(vl.host, hostname_g, sizeof(vl.host));
    sstrncpy(vl.plugin, "smaps", sizeof(vl.plugin));
    sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));
    sstrncpy(vl.type, "memory", sizeof(vl.type));  // Use type defined in types.db
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

    plugin_dispatch_values(&vl);
}

static void smaps_submit(const char *plugin_instance, struct smaps_info *info) {
    _smaps_submit(plugin_instance, "rss", info->rss);
    _smaps_submit(plugin_instance, "pss", info->pss);
    _smaps_submit(plugin_instance, "uss", info->uss);
}

// v is undefined if this does not return 0.
static int mapstrtou64(const char *s, uint64_t *v) {
    char *ccheck;
    long val;

    while (*s) {
        if (*s == ':') {
            s++;
            break;
        } else if (*s == '\n' || *s == '\0') {
            return 1;
        }
        s++;
    }

    val = strtol(s, &ccheck, 10);
    if (*ccheck != ' ' || val < 0)
        return 1;

    *v = (uint64_t)val;

    // This requires a suffix, pages are at least 4kB.
    // Currently the kernel will always report this as kB (fs/proc/task_mmu.c).
    switch (*(ccheck + 1)) {
    case 'k':
    case 'K':
        *v *= (1024);
        break;
    case 'M':
        *v *= (1048576);
        break;
    case 'G':
        *v *= (1073741824);
        break;
    case 'T':
        *v *= (1099511627776ULL);
        break;
    default:
        return 1;
    }
    return 0;
}

static int smaps_read(void) {
    struct smaps_info total_info = {};
    DIR *dirp = NULL;
    struct dirent *entry = NULL;
    FILE *stream = NULL;
    char *buf = NULL;
    char *ccheck;
    void *hist = hist_init();
    size_t buf_size;
    ssize_t len;
    long pid_max;
    long pval;
    int ret = 1;
    char path[128];

    stream = fopen("/proc/sys/kernel/pid_max", "r");
    if (!stream)
        goto done;

    if ((len = getline(&buf, &buf_size, stream)) == -1)
        goto done;
    fclose(stream);
    stream = NULL;

    pid_max = strtol(buf, &ccheck, 10);
    if ((*ccheck != '\0' && *ccheck != '\n') || pid_max <= 0)
        goto done;

    dirp = opendir("/proc");
    if (!dirp)
        goto done;

    while ((entry = readdir(dirp)) != NULL) {
        struct smaps_info proc_info = {};
        uint64_t val;
        pval = strtol(entry->d_name, &ccheck, 10);
        if (*ccheck != '\0' || pval <= 0 || pval > pid_max) {
            continue;
        }

        if (snprintf(path, sizeof(path), "/proc/%s/smaps", entry->d_name)
                <= 0)
            goto done;

        stream = fopen(path, "r");
        if (!stream) {
            // Either permission denied or the process went away.
            continue;
        }

        posix_fadvise(fileno(stream), 0, 0, POSIX_FADV_SEQUENTIAL);

        errno = 0;
        while ((len = getline(&buf, &buf_size, stream)) != -1) {
            if (!strncmp(buf, "Rss:", 4)) {
                if (!mapstrtou64(buf, &val))
                    proc_info.rss += val;
            } else if (!strncmp(buf, "Pss:", 4)) {
                if (!mapstrtou64(buf, &val))
                    proc_info.pss += val;
            } else if (!strncmp(buf, "Private_", 8)) {
                // Private_Clean and Private_Dirty are USS.
                if (!mapstrtou64(buf, &val))
                    proc_info.uss += val;
            }
        }

        fclose(stream);
        stream = NULL;

        // Getline failed, skip this entry.
        if (errno) {
            continue;
        }

        total_info.rss += proc_info.rss;
        total_info.pss += proc_info.pss;
        total_info.uss += proc_info.uss;

        if (proc_count) {
            if (snprintf(path, sizeof(path), "/proc/%s/comm", entry->d_name)
                    <= 0)
                goto done;
            stream = fopen(path, "r");
            if (stream) {
                if ((len = getline(&buf, &buf_size, stream)) >= 1) {
                    // comm should have a newline.
                    if (buf[len - 1] == '\n')
                        buf[len - 1] = '\0';
                    if (proc_find(buf, hist))
                        smaps_submit(buf, &proc_info);
                }
                fclose(stream);
                stream = NULL;
            }
        }
    }

    smaps_submit("total", &total_info);

    ret = 0;

done:
    if (dirp)
        closedir(dirp);

    if (buf)
        free(buf);

    if (stream)
        fclose(stream);

    if (hist)
        free(hist);

    return ret;
}

void module_register(void) {
    plugin_register_config("smaps", smaps_config, smaps_keys,
            smaps_keys_count);
    plugin_register_read("smaps", smaps_read);
    plugin_register_shutdown("smaps", smaps_shutdown);
}

