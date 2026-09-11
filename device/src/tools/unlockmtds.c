#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>

// From linux/mtd/mtd.h
// This struct is not complete just up to the point of things
// we're interested in.

typedef uint8_t u_char;
struct mtd_info {
    u_char type;
    uint32_t flags;
    uint64_t size;   // Total size of the MTD
    uint32_t erasesize;
    uint32_t writesize;
    uint32_t writebufsize;
    uint32_t oobsize;   // Amount of OOB data per block (e.g. 16)
    uint32_t oobavail;  // Available OOB bytes per block
    unsigned int erasesize_shift;
    unsigned int writesize_shift;
    unsigned int erasesize_mask;
    unsigned int writesize_mask;
    unsigned int bitflip_threshold;
    const char *name;
};

#define DPRINTF(__FMT__, ...) \
    if (check) fprintf(stderr, __FMT__, ## __VA_ARGS__)

#define EXITMSG(__FMT__, ...) \
    do { \
        fprintf(stderr, __FMT__, ## __VA_ARGS__); \
        exit(1); \
    } while (0)

// It should actually be around 3MB but just to be safe.
#define KERNEL_SIZE                                 (4*1024*1024)
#define KERNEL_PHYSICAL_START_ADDR                  (0x60000UL)
#define KERNEL_VIRTUAL_START_ADDR                   (0x80060000UL)
#define PAGE_SIZE                                   (4*1024UL)
#define PAGE_MASK                                   (~(PAGE_SIZE - 1))
#define KADDR_INVALID                               ((kaddr)-1)

#define PHYS_ADDR(virt) \
    (((virt) < KERNEL_VIRTUAL_START_ADDR) ? KERNEL_VIRTUAL_START_ADDR : \
    ((virt) - KERNEL_PHYSICAL_START_ADDR + KERNEL_PHYSICAL_START_ADDR))

#define VIRT_ADDR(phys) \
    (((phys) < KERNEL_VIRTUAL_START_ADDR) ? KADDR_INVALID : \
    ((phys) + KERNEL_PHYSICAL_START_ADDR - KERNEL_PHYSICAL_START_ADDR))

#define MND_STR                                     ("__mtd_next_device")

typedef off_t kaddr;

typedef struct {
    kaddr virt_base;
    uint8_t *map_base;
    size_t map_size;
} KernelMap;

static bool check = false;

static int supported_kernel(const char *ver) {
    const char * const kernel_versions[2] = {
        "3.8.13",
        "3.7.9"
    };
    int i;
    for (i = 0; i < sizeof(kernel_versions)/sizeof(char *); i++) {
        if (!strcmp(kernel_versions[i], ver)) {
            return 0;
        }
    }

    return 1;
}

static void kmap(int memfd, KernelMap *kernel_map, kaddr virt_addr) {
    kaddr phys_base = PHYS_ADDR(virt_addr & PAGE_MASK);

    if (kernel_map->map_base) {
        munmap(kernel_map->map_base, kernel_map->map_size);
        EXITMSG("kernel_map already open\n");
    }

    kernel_map->map_base = mmap(0, PAGE_SIZE, PROT_READ | PROT_WRITE,
            MAP_SHARED, memfd, phys_base);
    if (kernel_map->map_base == (void *)-1) {
        EXITMSG("mmap failed on kaddr %jd\n", (intmax_t)virt_addr);
    }

    kernel_map->virt_base = virt_addr & PAGE_MASK;
    kernel_map->map_size = PAGE_SIZE;

    //printf("%jx %p %zd\n", (intmax_t)kernel_map->virt_base,
    //        kernel_map->map_base, kernel_map->map_size);
}

static void kunmap(KernelMap *kernel_map) {
    if (kernel_map->map_base) {
        munmap(kernel_map->map_base, kernel_map->map_size);
        kernel_map->virt_base = 0;
        kernel_map->map_base = NULL;
        kernel_map->map_size = 0;
    }
}

static kaddr kmemmem(int memfd, kaddr haystack, size_t haystacklen,
        const void *needle, size_t needlelen) {
    KernelMap kernel_map = {};
    uint8_t *ret;
    kaddr base;
    kaddr found = 0;

    for (base = haystack ; base < (haystack + haystacklen);
            base += PAGE_SIZE) {
        kmap(memfd, &kernel_map, base);
        ret = memmem(kernel_map.map_base, kernel_map.map_size, needle,
                needlelen);
        //printf("ret: %p\n", ret);
        if (ret) {
            found = VIRT_ADDR(((kaddr)(ret - kernel_map.map_base))
                    + PHYS_ADDR(kernel_map.virt_base));
            break;
        }

        kunmap(&kernel_map);
    }

    return found;
}

static void krw(int memfd, void *ptr, kaddr addr, size_t size, bool read) {
    KernelMap kernel_map = {};
    uint8_t *buf = ptr;
    size_t copy_size;
    size_t page_offset;
    size_t page_remaining;
    size_t remaining = size;

    while (remaining) {
        kmap(memfd, &kernel_map, addr);

        // Get offset of the requested kaddr in the page.
        page_offset = addr - kernel_map.virt_base;

        // Figure out how much we can copy out of this page.
        page_remaining = kernel_map.map_size - page_offset;
        copy_size = remaining < page_remaining ? remaining : page_remaining;

        //printf("%jx %p %zd\n", (intmax_t)kernel_map.virt_base,
        //        kernel_map.map_base, kernel_map.map_size);
        //printf("copy_size: 0x%zx page_offset: 0x%zx page_remaining: 0x%zx "
        //        "remaining: 0x%zx\n", copy_size, page_offset, page_remaining,
        //        remaining);

        if (read) {
            memcpy(buf, kernel_map.map_base + page_offset, copy_size);
        } else {
            memcpy(kernel_map.map_base + page_offset, buf, copy_size);
        }

        kunmap(&kernel_map);

        buf += copy_size;
        addr += copy_size;
        remaining -= copy_size;
    }
}

static void kread(int memfd, void *dest, kaddr addr, size_t size) {
    krw(memfd, dest, addr, size, true);
}

static void kwrite(int memfd, void *dest, kaddr addr, size_t size) {
    krw(memfd, dest, addr, size, false);
}

// We use this function to find mtd_idr which we can use to get
// the mtd_info structs (from mtdcore.c).
//struct mtd_info *__mtd_next_device(int i)
//{
//    return idr_get_next(&mtd_idr, &i);
//}
//
// Which with the common kernel build arguments is compiled
// into:
//objdump -b binary -m mips -EB -D mtdnext.bin
//00000000 <.data>:
//   0:   27bdffe8    addiu   sp,sp,-24
//   4:   afa40018    sw  a0,24(sp)
//   8:   3c048035    lui a0,0x8035
//   c:   27a50018    addiu   a1,sp,24
//  10:   afbf0014    sw  ra,20(sp)
//  14:   0c05b3aa    jal 0x16cea8
//  18:   24848520    addiu   a0,a0,-31456
//  1c:   8fbf0014    lw  ra,20(sp)
//  20:   03e00008    jr  ra
//  24:   27bd0018    addiu   sp,sp,24
//
// The pointer for mtd_idr is constructed by the instructions
// at 8 and 18.
//
// get_mtd_idr_from_mnd_func does no checks to sanity that we are at
// the correct function.

#define MND_FUNC_READ_SIZE                          (28)
#define MTD_IDR_HIGH_NIBBLE_OFFSET                  (10)
#define MTD_IDR_ADDIU_OFFSET                        (26)

static kaddr get_mtd_idr_from_mnd_func(int memfd, kaddr func_addr) {
    uint8_t buf[MND_FUNC_READ_SIZE];
    uint16_t high_nibble;
    // Even though this instruction is add unsigned we need
    // to treat it as a signed value for sign extension
    // when creating the kaddr.
    int16_t addiu;

    kread(memfd, buf, func_addr, sizeof(buf));

    memcpy(&high_nibble, buf + MTD_IDR_HIGH_NIBBLE_OFFSET,
            sizeof(high_nibble));
    memcpy(&addiu, buf + MTD_IDR_ADDIU_OFFSET, sizeof(addiu));

    return (((kaddr)high_nibble) << 16) + addiu;
}

// The kernel idr structure is a way of creating unique ids
// and mapping each one to a pointer.  Most of the complexity
// has to do with performance and atomicity that we don't
// need to worry about.
//
//struct idr_layer {
//    unsigned long        bitmap; /* A zero bit means "space here" */
//    struct idr_layer __rcu  *ary[1<<IDR_BITS];
//    int          count;  /* When zero, we can release it */
//    int          layer;  /* distance from leaf */
//    struct rcu_head      rcu_head;
//};
//
//struct idr {
//    struct idr_layer __rcu *top;
//    struct idr_layer *id_free;
//    int       layers; /* only valid without concurrent changes */
//    int       id_free_cnt;
//    spinlock_t    lock;
//};
static kaddr *get_mtd_info_structs(int memfd, kaddr mtd_idr_addr,
        int *count) {
    uint8_t *buf;
    kaddr *mtd_info_addrs;
    kaddr top_addr;
    unsigned long bitmap;
    int read_count;
    int mtd_info_slot = 0;
    int i;

    kread(memfd, &top_addr, mtd_idr_addr, sizeof(kaddr));

    DPRINTF("top_addr %jx\n", (intmax_t)top_addr);

    kread(memfd, &bitmap, top_addr, sizeof(bitmap));

    DPRINTF("bitmap %lx\n", bitmap);

    *count = __builtin_popcountl(bitmap);
    if (!*count) {
        return NULL;
    }

    read_count = (int)(sizeof(unsigned long)*8 - __builtin_clzl(bitmap));

    DPRINTF("mtd_info structs count: %d read_count: %d\n", *count,
            read_count);

    buf = malloc(sizeof(kaddr) * read_count);
    mtd_info_addrs = malloc(sizeof(kaddr) * *count);

    if (!buf || !mtd_info_addrs) {
        EXITMSG("out of memory\n");
    }

    for (i = 0; i < read_count; i++) {
        if ((1<<i) & bitmap) {
            kread(memfd, mtd_info_addrs + mtd_info_slot,
                    top_addr + sizeof(unsigned long) + (sizeof(kaddr) * i),
                    sizeof(kaddr));
            DPRINTF("found mtd_info at %jx\n",
                    (intmax_t)mtd_info_addrs[mtd_info_slot]);
            mtd_info_slot++;
        }
    }

    return mtd_info_addrs;
}

#define MTD_NAME_MAX_LEN                            (64)

static void dump_mtd_info(int memfd, kaddr *mtd_info_addrs, int count) {
    struct mtd_info mtd_info;
    char mtd_name[MTD_NAME_MAX_LEN + 1] = {};
    int i;

    for (i = 0; i < count; i++) {
        kread(memfd, &mtd_info, mtd_info_addrs[i], sizeof(mtd_info));
        kread(memfd, mtd_name, (kaddr)mtd_info.name, MTD_NAME_MAX_LEN);
        DPRINTF("mtd_info: %d\n", i);
        DPRINTF("  flags: %08x\n", mtd_info.flags);
        DPRINTF("  size: %016" PRIx64 "\n", mtd_info.size);
        DPRINTF("  name: %s\n", mtd_name);
    }
}

// From uapi/mtd/mtd-abi.h
#define MTD_WRITEABLE       0x400   /* Device is writeable */
#define MTD_BIT_WRITEABLE   0x800   /* Single bits can be flipped */
#define MTD_NO_ERASE        0x1000  /* No erase necessary */
#define MTD_POWERUP_LOCK    0x2000  /* Always locked after reset */

static void disable_write_protection(int memfd, kaddr *mtd_info_addrs,
        int count) {
    uint32_t flags = MTD_WRITEABLE | MTD_BIT_WRITEABLE;
    int i;

    for (i = 0; i < count; i++) {
        kwrite(memfd, &flags, mtd_info_addrs[i]
                + offsetof(struct mtd_info, flags), sizeof(flags));
    }
}

int main(int argc, char **argv) {
    struct utsname utsname;
    kaddr mnd_str_addr;
    kaddr mnd_str_ptr_addr;
    kaddr mnd_func_ptr = 0;
    kaddr mtd_idr_addr;
    kaddr *mtd_info_structs = NULL;
    int mtd_info_count = 0;
    int ret;
    int memfd;

    if (sizeof(kaddr) != sizeof(void *)) {
        DPRINTF("size of kaddr != void *\n");
        return 1;
    }

    if (argc >= 2 && !strcmp(argv[1], "check")) {
        check = true;
        DPRINTF("%s sanity check\n", argv[0]);
    }

    ret = uname(&utsname);
    if (ret) {
        fprintf(stderr, "uname failed %d\n", ret);
        return ret;
    }

    // None of this is guarenteed to work so check if this is a supported
    // kernel.  Even then different configs, compilers, patches could cause
    // this to fail.
    if (supported_kernel(utsname.release)) {
        fprintf(stderr, "Unsupported kernel version %s\n", utsname.release);
        if (!check) {
            fprintf(stderr,
                    "Please re-run with check to sanity the data and add this\n"
                    "to the list of supported kernels\n");
            return 1;
        }
    } else {
        DPRINTF("Supported kernel version %s\n", utsname.release);
    }


    memfd = open("/dev/mem", O_RDWR | O_SYNC);
    if (memfd == -1) {
        EXITMSG("could not open /dev/mem\n");
    }

    // Find the function name in the symbol string table.  This will
    // only work for function that are exported with EXPORT_SYMBOL_*.
    mnd_str_addr = kmemmem(memfd, KERNEL_VIRTUAL_START_ADDR, KERNEL_SIZE,
            MND_STR, sizeof(MND_STR));
    if (!mnd_str_addr) {
        EXITMSG("Could not find symbol string %s\n", MND_STR);
    }
    DPRINTF("Found symbol name %s at %jx\n", MND_STR, (intmax_t)mnd_str_addr);

    // Find a pointer to the symbol string address.
    mnd_str_ptr_addr = kmemmem(memfd, KERNEL_VIRTUAL_START_ADDR, KERNEL_SIZE,
            &mnd_str_addr, sizeof(mnd_str_addr));
    if (!mnd_str_ptr_addr) {
        EXITMSG("Could not find symbol name ptr for %s\n", MND_STR);
    }
    DPRINTF("Found symbol name ptr for %s at %jx\n", MND_STR,
            (intmax_t)mnd_str_ptr_addr);

    // The each entry in the symbol table is:
    // struct {
    //     void *func_ptr;
    //     const char *func_name;
    // };
    kread(memfd, &mnd_func_ptr, mnd_str_ptr_addr - sizeof(kaddr),
            sizeof(kaddr));
    if (!mnd_func_ptr) {
        EXITMSG("Could not read function ptr for %s\n", MND_STR);
    }
    DPRINTF("%s function at %jx\n", MND_STR, (intmax_t)mnd_func_ptr);

    mtd_idr_addr = get_mtd_idr_from_mnd_func(memfd, mnd_func_ptr);
    DPRINTF("mtd_idr located at %jx\n", (intmax_t)mtd_idr_addr);

    mtd_info_structs = get_mtd_info_structs(memfd, mtd_idr_addr,
            &mtd_info_count);
    DPRINTF("found %d mtd_info structs\n", mtd_info_count);

    if (check) {
        dump_mtd_info(memfd, mtd_info_structs, mtd_info_count);
    } else {
        disable_write_protection(memfd, mtd_info_structs, mtd_info_count);
    }

    if (mtd_info_structs) {
        free(mtd_info_structs);
    }

    return 0;
}
