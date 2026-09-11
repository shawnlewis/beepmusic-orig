# TODO:
#     - Generalize this so it works on other machines
#     - Change other make_cross.sh to use this format (to do that, build using
#       the openwrt build system, and copy what it does).
#     - Merge with make-cross.sh
#     - Fix make-cross.sh to automatically find openwrt root
#     - Move to beep/tools, and symlink it from ~/bin

make \
    PATH="$BEEP_SRC_ROOT/device/openwrt/staging_dir/toolchain-mips_r2_gcc-4.6-linaro_uClibc-0.9.33.2/bin:$BEEP_SRC_ROOT/device/openwrt/staging_dir/host/bin:$BEEP_SRC_ROOT/device/openwrt/staging_dir/toolchain-mips_r2_gcc-4.6-linaro_uClibc-0.9.33.2/bin:$BEEP_SRC_ROOT/device/openwrt/staging_dir/host/bin:$PATH" \
    ARCH="mips" \
    CROSS_COMPILE="mips-openwrt-linux-uclibc-" \
    TOOLPREFIX="mips-openwrt-linux-uclibc-" \
    TOOLPATH="mips-openwrt-linux-uclibc-" \
    KERNELPATH="$BEEP_SRC_ROOT/device/openwrt/build_dir/target-mips_r2_uClibc-0.9.33.2/linux-ar71xx_generic/linux-3.8.13" \
    LDOPTS=" " all
