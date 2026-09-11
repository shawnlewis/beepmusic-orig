// Usage:
// Host:
// socat UDP-LISTEN:4455,reuseaddr,fork ./remote_trace.txt
// Device:
// root@beep-003081:/tmp# NETMTRACE_NET=udp:192.168.1.20:4455 LD_LIBRARY_PATH=`pwd` LD_PRELOAD=libnetmtrace.so ./freemem

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dlfcn.h>
#include <link.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>


#define TRACE_BUF_SIZE                              (1024)
#define TRACE_RETURN_ADDRESS(__LVL__) \
        (__builtin_extract_return_addr(__builtin_return_address(__LVL__)))

#define NMT_MAX_NET_ENV_STR                         (25)

#define NMT_TYPE_MALLOC                             (0)
#define NMT_TYPE_CALLOC                             (1)
#define NMT_TYPE_REALLOC                            (2)
#define NMT_TYPE_FREE                               (3)
#define NMT_TYPE_MEMALIGN                           (4)
#define NMT_TYPE_VALLOC                             (5)
#define NMT_TYPE_POSIX_MEMALIGN                     (6)
#define NMT_TYPE_CFREE                              (7)
#define NMT_TYPE_PVALLOC                            (8)

#define NMT_TRACE_NET_NONE                          (0)
#define NMT_TRACE_NET_UDP                           (1)
#define NMT_TRACE_NET_TCP                           (2)

struct nmt_record {
    void *caller;
    int type;
    union {
        struct {
            size_t size;
            void *ret;
        } malloc;

        struct {
            size_t nmemb;
            size_t size;
            void *ret;
        } calloc;

        struct {
            void *ptr;
            size_t size;
            void *ret;
        } realloc;

        struct {
            void *ptr;
        } free;

        struct {
            size_t alignment;
            size_t size;
            void *ret;
        } memalign;

        struct {
            size_t size;
            void *ret;
        } valloc;

        struct {
            void **memptr;
            size_t alignment;
            size_t size;
            int ret;
        } posix_memalign;

        struct {
            void *ptr;
        } cfree;

        struct {
            size_t size;
            void *ret;
        } pvalloc;
    } data;
};

static void *(*sys_malloc)(size_t);
static void *(*sys_calloc)(size_t, size_t);
static void *(*sys_realloc)(void *, size_t);
static void (*sys_free)(void *);
static void *(*sys_memalign)(size_t, size_t);
static void *(*sys_valloc)(size_t);
static int (*sys_posix_memalign)(void **, size_t, size_t);
#ifndef __UCLIBC__
static void (*sys_cfree)(void *);
static void *(*sys_pvalloc)(size_t);
#endif

static char trace_buf[TRACE_BUF_SIZE];
static char *trace_buf_ptr;
static unsigned long trace_id;
static int trace_stderr;
static int trace_net;
static struct sockaddr_in sa_in;
static socklen_t sa_len;
static int net_sock;
static void *exec_vaddr;

static __thread int disable_trace;

void nmt_err(const char *str) __attribute__((noreturn));
void nmt_err(const char *str) {
    fputs("NETMTRACE: ", stderr);
    fputs(str, stderr);
    fputs("\n", stderr);
    fflush(stderr);
    exit(1);
}

static int phdr_cb(struct dl_phdr_info *info, size_t size, void *data) {
    int i;

    for (i = 0; i < info->dlpi_phnum; i++) {
        if (info->dlpi_phdr[i].p_type == PT_LOAD) {
            exec_vaddr = (void *)(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
            break;
        }
    }

    // First callback will always be for the executable, return non-zero to
    // stop the callbacks.
    return 1;
}

static void init_netmtrace(void) {
    char *envp;

    disable_trace = 1;

    trace_buf_ptr = trace_buf;

    void *(*dlsym_malloc)(size_t);
    void *(*dlsym_calloc)(size_t, size_t);
    void *(*dlsym_realloc)(void *, size_t);
    void (*dlsym_free)(void *);
    void *(*dlsym_memalign)(size_t, size_t);
    void *(*dlsym_valloc)(size_t);
    int (*dlsym_posix_memalign)(void **, size_t, size_t);
#ifndef __UCLIBC__
    void (*dlsym_cfree)(void *);
    void *(*dlsym_pvalloc)(size_t);
#endif

    dlsym_malloc = (void *(*)(size_t))dlsym(RTLD_NEXT, "malloc");
    dlsym_calloc = (void *(*)(size_t, size_t))dlsym(RTLD_NEXT, "calloc");
    dlsym_realloc = (void *(*)(void *, size_t))dlsym(RTLD_NEXT, "realloc");
    dlsym_free = (void (*)(void *))dlsym(RTLD_NEXT, "free");
    dlsym_memalign = (void *(*)(size_t, size_t))dlsym(RTLD_NEXT, "memalign");
    dlsym_valloc = (void *(*)(size_t))dlsym(RTLD_NEXT, "valloc");
    dlsym_posix_memalign = (int (*)(void **, size_t, size_t))dlsym(RTLD_NEXT,
            "posix_memalign");
#ifndef __UCLIBC__
    dlsym_cfree = (void (*)(void *))dlsym(RTLD_NEXT, "cfree");
    dlsym_pvalloc = (void *(*)(size_t))dlsym(RTLD_NEXT, "pvalloc");
#endif

    if (!dlsym_malloc
            || !dlsym_calloc
            || !dlsym_realloc
            || !dlsym_free
            || !dlsym_memalign
            || !dlsym_valloc
            || !dlsym_posix_memalign
#ifndef __UCLIBC__
            || !dlsym_cfree
            || !dlsym_pvalloc
#endif
            ) {
        nmt_err("could not resolve all symbols");
    }

    sys_malloc = dlsym_malloc;
    sys_calloc = dlsym_calloc;
    sys_realloc = dlsym_realloc;
    sys_free = dlsym_free;
    sys_memalign = dlsym_memalign;
    sys_valloc = dlsym_valloc;
    sys_posix_memalign = dlsym_posix_memalign;
#ifndef __UCLIBC__
    sys_cfree = dlsym_cfree;
    sys_pvalloc = dlsym_pvalloc;
#endif

    // Get the executable load address.
    dl_iterate_phdr(phdr_cb, NULL);

#ifndef __UCLIBC__
    envp = secure_getenv("NETMTRACE_STDERR");
#else
    envp = getenv("NETMTRACE_STDERR");
#endif
    if (envp && *envp != '0')
        trace_stderr = 1;

#ifndef __UCLIBC__
    envp = secure_getenv("NETMTRACE_NET");
#else
    envp = getenv("NETMTRACE_NET");
#endif
    if (envp) {
        char *p;
        char *ccheck;
        long int port;
        char buf[NMT_MAX_NET_ENV_STR + 2];
        buf[NMT_MAX_NET_ENV_STR + 1] = '\0';

        strncpy(buf, envp, NMT_MAX_NET_ENV_STR + 1);
        if (strnlen(buf, NMT_MAX_NET_ENV_STR + 1) >= (NMT_MAX_NET_ENV_STR + 1)) {
            nmt_err("too long");
        }

        if (buf[0] == 'u' && buf[1] == 'd' && buf[2] == 'p' && buf[3] == ':') {
            trace_net = NMT_TRACE_NET_UDP;
        } else if (buf[0] == 't' && buf[1] == 'c' && buf[2] == 'p'
                && buf[3] == ':') {
            trace_net = NMT_TRACE_NET_TCP;
            nmt_err("tcp unsupported");
        } else {
            nmt_err("unknown protocol");
        }

        p = buf + 4;
        while (*p) {
            if (*p == ':')
                break;
            p++;
        }
        if (*p == '\0') {
            nmt_err("invalid address:port");
        }
        *p++ = '\0';
        port = strtol(p, &ccheck, 10);
        if (port <= 0 || port >= 65536 || *ccheck != '\0') {
            nmt_err("invalid port");
        }

        if (!inet_aton(buf + 4, &sa_in.sin_addr)) {
            nmt_err("invalid ip");
        }
        sa_in.sin_family = AF_INET;
        sa_in.sin_port = htons((uint16_t)port);

        net_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (net_sock == -1) {
            nmt_err("could not get socket");
        }

        sa_len = sizeof(struct sockaddr_in);
    }

    disable_trace = 0;
}

void __attribute__((constructor)) ctor_init_netmtrace(void) {
    if (!sys_malloc) {
        init_netmtrace();
    }
}

static void nmt_trace(struct nmt_record *rec) {
    unsigned long this_id = __sync_fetch_and_add(&trace_id, 1);
    Dl_info dl_info;
    int len;
    // Keep the char buffer on the stack.
    char buf[TRACE_BUF_SIZE];

    if (rec->caller) {
        dladdr(rec->caller, &dl_info);
        if (!dl_info.dli_fname) {
            dl_info.dli_fname = "unknown";
        } else if (dl_info.dli_fbase != exec_vaddr) {
            rec->caller = (void *)((uint8_t *)rec->caller -
                    (uint8_t *)dl_info.dli_fbase);
        }
    } else {
        dl_info.dli_fname = "unknown";
    }

    switch (rec->type) {
    case NMT_TYPE_MALLOC:
        len = snprintf(buf, TRACE_BUF_SIZE, "%08lx ma %s[%p] %zx %p\n",
                this_id,
                dl_info.dli_fname,
                rec->caller,
                rec->data.malloc.size,
                rec->data.malloc.ret);
        break;

    case NMT_TYPE_CALLOC:
        len = snprintf(buf, TRACE_BUF_SIZE, "%08lx ca %s[%p] %zx %zx %p\n",
                this_id,
                dl_info.dli_fname,
                rec->caller,
                rec->data.calloc.nmemb,
                rec->data.calloc.size,
                rec->data.calloc.ret);
        break;

    case NMT_TYPE_REALLOC:
        len = snprintf(buf, TRACE_BUF_SIZE, "%08lx re %s[%p] %p %zx %p\n",
                this_id,
                dl_info.dli_fname,
                rec->caller,
                rec->data.realloc.ptr,
                rec->data.realloc.size,
                rec->data.realloc.ret);
        break;

    case NMT_TYPE_FREE:
        len = snprintf(buf, TRACE_BUF_SIZE, "%08lx fr %s[%p] %p\n",
                this_id,
                dl_info.dli_fname,
                rec->caller,
                rec->data.free.ptr);
        break;

    case NMT_TYPE_MEMALIGN:
        len = snprintf(buf, TRACE_BUF_SIZE, "%08lx me %s[%p] %zx %zx %p\n",
                this_id,
                dl_info.dli_fname,
                rec->caller,
                rec->data.memalign.alignment,
                rec->data.memalign.size,
                rec->data.memalign.ret);
        break;

    case NMT_TYPE_VALLOC:
        len = snprintf(buf, TRACE_BUF_SIZE, "%08lx va %s[%p] %zx %p\n",
                this_id,
                dl_info.dli_fname,
                rec->caller,
                rec->data.valloc.size,
                rec->data.valloc.ret);
        break;

    case NMT_TYPE_POSIX_MEMALIGN:
        len = snprintf(buf, TRACE_BUF_SIZE, "%08lx po %s[%p] %p %zx %zx %d\n",
                this_id,
                dl_info.dli_fname,
                rec->caller,
                rec->data.posix_memalign.memptr ?
                        *(rec->data.posix_memalign.memptr) : NULL,
                rec->data.posix_memalign.alignment,
                rec->data.posix_memalign.size,
                rec->data.posix_memalign.ret);
        break;

#ifndef __UCLIBC__
    case NMT_TYPE_CFREE:
        len = snprintf(buf, TRACE_BUF_SIZE, "%08lx cf %s[%p] %p\n",
                this_id,
                dl_info.dli_fname,
                rec->caller,
                rec->data.free.ptr);
        break;

    case NMT_TYPE_PVALLOC:
        len = snprintf(buf, TRACE_BUF_SIZE, "%08lx pv %s[%p] %zx %p\n",
                this_id,
                dl_info.dli_fname,
                rec->caller,
                rec->data.pvalloc.size,
                rec->data.pvalloc.ret);
        break;
#endif

    default:
        return;
    }

    if (len >= TRACE_BUF_SIZE) {
        len = TRACE_BUF_SIZE;
        buf[len - 1] = '\0';
        buf[len - 2] = '\n';
        buf[len - 3] = '.';
        buf[len - 4] = '.';
        buf[len - 5] = '.';
    }

    if (trace_stderr)
        fputs(buf, stderr);

    if (trace_net) {
        sendto(net_sock, buf, len, MSG_DONTWAIT | MSG_NOSIGNAL,
                (struct sockaddr *)&sa_in, sa_len);
    }
}

// Used to allocate memory for dlsym while inside init_netmtrace.
// Need support for malloc, calloc and free.
static void *init_malloc(size_t size) {
    void *p = (void *)trace_buf_ptr;
    trace_buf_ptr += size;
    // Assume a oom event while doing dlsym is going to be fatal,
    // just exit now.
    if (trace_buf_ptr >= (trace_buf + TRACE_BUF_SIZE)) {
        nmt_err("oom during init");
    }
    return p;
}

void *malloc(size_t size) {
    if (__builtin_expect(sys_malloc != NULL, 1)) {
        if (!disable_trace) {
            struct nmt_record rec;

            disable_trace = 1;

            rec.caller = TRACE_RETURN_ADDRESS(0);
            rec.type = NMT_TYPE_MALLOC;
            rec.data.malloc.size = size;
            rec.data.malloc.ret = sys_malloc(size);
            nmt_trace(&rec);

            disable_trace = 0;

            return rec.data.malloc.ret;
        } else {
            return sys_malloc(size);
        }
    } else {
        // malloc has been called before sys_malloc has been setup.  Either
        // malloc was called from a constructor before ours or dlsym is
        // calling into here and we need to return space on the statically
        // allocated trace_buf.
        if (disable_trace) {
            // Inside init_netmtrace.
            return init_malloc(size);
        } else {
            // From another constructor.
            init_netmtrace();
            return malloc(size);
        }
    }
}

void *calloc(size_t nmemb, size_t size) {
    if (__builtin_expect(sys_calloc != NULL, 1)) {
        if (!disable_trace) {
            struct nmt_record rec;

            disable_trace = 1;

            rec.caller = TRACE_RETURN_ADDRESS(0);
            rec.type = NMT_TYPE_CALLOC;
            rec.data.calloc.nmemb = nmemb;
            rec.data.calloc.size = size;
            rec.data.calloc.ret = sys_calloc(nmemb, size);
            nmt_trace(&rec);

            disable_trace = 0;

            return rec.data.calloc.ret;
        } else {
            return sys_calloc(nmemb, size);
        }
    } else {
        if (disable_trace) {
            // Inside init_netmtrace.
            return init_malloc(nmemb * size);
        } else {
            // From another constructor.
            init_netmtrace();
            return calloc(nmemb, size);
        }
    }
}

void *realloc(void *ptr, size_t size) {
    if (__builtin_expect(sys_realloc != NULL, 1)) {
        if (!disable_trace) {
            struct nmt_record rec;

            disable_trace = 1;

            rec.caller = TRACE_RETURN_ADDRESS(0);
            rec.type = NMT_TYPE_REALLOC;
            rec.data.realloc.ptr = ptr;
            rec.data.realloc.size = size;
            rec.data.realloc.ret = sys_realloc(ptr, size);
            nmt_trace(&rec);

            disable_trace = 0;

            return rec.data.realloc.ret;
        } else {
            return sys_realloc(ptr, size);
        }
    } else {
        if (disable_trace) {
            // Inside init_netmtrace.
            nmt_err("realloc not supported from init_netmtrace");
        } else {
            // From another constructor.
            init_netmtrace();
            return realloc(ptr, size);
        }
    }
}

void free(void *ptr) {
    if (__builtin_expect(sys_free != NULL, 1)) {
        if (!disable_trace) {
            struct nmt_record rec;

            disable_trace = 1;

            rec.caller = TRACE_RETURN_ADDRESS(0);
            rec.type = NMT_TYPE_FREE;
            rec.data.free.ptr = ptr;
            sys_free(ptr);
            nmt_trace(&rec);

            disable_trace = 0;

            return;
        } else {
            sys_free(ptr);
            return;
        }
    } else {
        if (disable_trace) {
            // Inside init_netmtrace, do nothing trace_buf should be large
            // enough to support bootstrapping without frees.
            return;
        } else {
            // From another constructor.  A free is valid to come before any
            // of allocation as it could just be free(NULL).
            init_netmtrace();
            free(ptr);
            return;
        }
    }
}

void *memalign(size_t alignment, size_t size) {
    if (__builtin_expect(sys_memalign != NULL, 1)) {
        if (!disable_trace) {
            struct nmt_record rec;

            disable_trace = 1;

            rec.caller = TRACE_RETURN_ADDRESS(0);
            rec.type = NMT_TYPE_MEMALIGN;
            rec.data.memalign.alignment = alignment;
            rec.data.memalign.size = size;
            rec.data.memalign.ret = sys_memalign(alignment, size);
            nmt_trace(&rec);

            disable_trace = 0;

            return rec.data.memalign.ret;
        } else {
            return sys_memalign(alignment, size);
        }
    } else {
        if (disable_trace) {
            // Inside init_netmtrace.
            nmt_err("memalign not supported from init_netmtrace");
        } else {
            // From another constructor.
            init_netmtrace();
            return memalign(alignment, size);
        }
    }
}

void *valloc(size_t size) {
    if (__builtin_expect(sys_valloc != NULL, 1)) {
        if (!disable_trace) {
            struct nmt_record rec;

            disable_trace = 1;

            rec.caller = TRACE_RETURN_ADDRESS(0);
            rec.type = NMT_TYPE_VALLOC;
            rec.data.valloc.size = size;
            rec.data.valloc.ret = sys_valloc(size);
            nmt_trace(&rec);

            disable_trace = 0;

            return rec.data.valloc.ret;
        } else {
            return sys_valloc(size);
        }
    } else {
        if (disable_trace) {
            // Inside init_netmtrace.
            nmt_err("valloc not supported from init_netmtrace");
        } else {
            // From another constructor.
            init_netmtrace();
            return valloc(size);
        }
    }
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
    if (__builtin_expect(sys_posix_memalign != NULL, 1)) {
        if (!disable_trace) {
            struct nmt_record rec;

            disable_trace = 1;

            rec.caller = TRACE_RETURN_ADDRESS(0);
            rec.type = NMT_TYPE_POSIX_MEMALIGN;
            rec.data.posix_memalign.memptr = memptr;
            rec.data.posix_memalign.alignment = alignment;
            rec.data.posix_memalign.size = size;
            rec.data.posix_memalign.ret = sys_posix_memalign(memptr, alignment, size);
            nmt_trace(&rec);

            disable_trace = 0;

            return rec.data.posix_memalign.ret;
        } else {
            return sys_posix_memalign(memptr, alignment, size);
        }
    } else {
        if (disable_trace) {
            // Inside init_netmtrace.
            nmt_err("posix_memalign not supported from init_netmtrace");
        } else {
            // From another constructor.
            init_netmtrace();
            return posix_memalign(memptr, alignment, size);
        }
    }
}

#ifndef __UCLIBC__
void cfree(void *ptr) {
    if (__builtin_expect(sys_cfree != NULL, 1)) {
        if (!disable_trace) {
            struct nmt_record rec;

            disable_trace = 1;

            rec.caller = TRACE_RETURN_ADDRESS(0);
            rec.type = NMT_TYPE_CFREE;
            rec.data.cfree.ptr = ptr;
            sys_cfree(ptr);
            nmt_trace(&rec);

            disable_trace = 0;

            return;
        } else {
            sys_cfree(ptr);
            return;
        }
    } else {
        if (disable_trace) {
            // Inside init_netmtrace.
            nmt_err("cfree not supported from init_netmtrace");
            return;
        } else {
            // From another constructor.
            init_netmtrace();
            cfree(ptr);
            return;
        }
    }
}

void *pvalloc(size_t size) {
    if (__builtin_expect(sys_pvalloc != NULL, 1)) {
        if (!disable_trace) {
            struct nmt_record rec;

            disable_trace = 1;

            rec.caller = TRACE_RETURN_ADDRESS(0);
            rec.type = NMT_TYPE_PVALLOC;
            rec.data.pvalloc.size = size;
            rec.data.pvalloc.ret = sys_pvalloc(size);
            nmt_trace(&rec);

            disable_trace = 0;

            return rec.data.pvalloc.ret;
        } else {
            return sys_pvalloc(size);
        }
    } else {
        if (disable_trace) {
            // Inside init_netmtrace.
            nmt_err("pvalloc not supported from init_netmtrace");
        } else {
            // From another constructor.
            init_netmtrace();
            return pvalloc(size);
        }
    }
}
#endif
