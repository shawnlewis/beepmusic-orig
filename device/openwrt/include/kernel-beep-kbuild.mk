# include this *after* KERNEL_MAKEOPTS definition in include/kernel-defaults.mk.

# pr_debug(...) and printk(KERN_DEBUG ...) are filtered out by default unless
# the file is compiled with DEBUG defined.  KBuild allows per-file CFLAGS with
# CFLAGS_<file>.o=<flag>.  The file does not include any preceding directory.
# This also means for common file name debug printks will be disabled for
# all file that match.

#ifdef CONFIG_BEEP_KBUILD_ARGS_NONE
#endif

ifdef CONFIG_BEEP_KBUILD_ARGS_DEBUG_ALL
  KERNEL_MAKEOPTS += \
	EXTRA_CFLAGS="-DDEBUG"
endif

# Disable noisy squashfs, i2c, mtd and net files.
ifdef CONFIG_BEEP_KBUILD_ARGS_DEBUG_TRIM
  KERNEL_MAKEOPTS += \
	EXTRA_CFLAGS="-DDEBUG" \
	CFLAGS_inode.o="-UDEBUG" \
	CFLAGS_cache.o="-UDEBUG" \
	CFLAGS_namei.o="-UDEBUG" \
	CFLAGS_symlink.o="-UDEBUG" \
	CFLAGS_dir.o="-UDEBUG" \
	CFLAGS_block.o="-UDEBUG" \
	CFLAGS_file.o="-UDEBUG" \
	CFLAGS_i2c-core.o="-UDEBUG" \
	CFLAGS_i2c-dev.o="-UDEBUG" \
	CFLAGS_mtdblock.o="-UDEBUG" \
	CFLAGS_m25p80.o="-UDEBUG" \
	CFLAGS_ip_tables.o="-UDEBUG"
endif

ifdef CONFIG_BEEP_KBUILD_ARGS_USE_CUSTOM
  KERNEL_MAKEOPTS += $(strip $(call qstrip,$(CONFIG_BEEP_KBUILD_ARGS_CUSTOM)))
endif
