PATH="../../../../openwrt/staging_dir/toolchain-mips_r2_gcc-4.6-linaro_uClibc-0.9.33.2/bin:../../../../openwrt/staging_dir/host/bin:../../../../openwrt/staging_dir/toolchain-mips_r2_gcc-4.6-linaro_uClibc-0.9.33.2/bin:../../../../openwrt/staging_dir/host/bin:$PATH" \
    STAGING_DIR="../../../../openwrt/staging_dir/target-mips_r2_uClibc-0.9.33.2" \
    ARCH="mips" \
    CC="mips-openwrt-linux-uclibc-gcc" \
    CFLAGS="-Os -pipe -mips32r2 -mtune=34kc -fno-caller-saves -mno-branch-likely -g3 -fhonour-copts -Wno-error=unused-but-set-variable -msoft-float -mips16 -minterlink-mips16 " \
    CPPFLAGS="-I../../../../openwrt/staging_dir/target-mips_r2_uClibc-0.9.33.2/usr/include -I../../../../openwrt/staging_dir/target-mips_r2_uClibc-0.9.33.2/include -I../../../../openwrt/staging_dir/toolchain-mips_r2_gcc-4.6-linaro_uClibc-0.9.33.2/usr/include -I../../../../openwrt/staging_dir/toolchain-mips_r2_gcc-4.6-linaro_uClibc-0.9.33.2/include -I../../../../openwrt/staging_dir/target-mips_r2_uClibc-0.9.33.2/usr/include/collectd" \
    LDFLAGS="-L../../../../openwrt/staging_dir/target-mips_r2_uClibc-0.9.33.2/usr/lib -L../../../../openwrt/staging_dir/target-mips_r2_uClibc-0.9.33.2/lib -L../../../../openwrt/staging_dir/toolchain-mips_r2_gcc-4.6-linaro_uClibc-0.9.33.2/usr/lib -L../../../../openwrt/staging_dir/toolchain-mips_r2_gcc-4.6-linaro_uClibc-0.9.33.2/lib " \
    make all
