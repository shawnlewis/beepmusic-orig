SCRIPT=$(readlink -f $0)
# Absolute path this script is in. /home/user/bin
SCRIPTPATH=`dirname $SCRIPT`

MIPS_GDB=$SCRIPTPATH/../openwrt/staging_dir/toolchain-mips_r2_gcc-4.6-linaro_uClibc-0.9.33.2/bin/mips-openwrt-linux-gdb

SOLIB_PATH=$SCRIPTPATH/../openwrt/staging_dir/toolchain-mips_r2_gcc-4.6-linaro_uClibc-0.9.33.2/lib
SOLIB_PATH=$SCRIPTPATH/../openwrt/staging_dir/target-mips_r2_uClibc-0.9.33.2/:$SOLIB_PATH
SOLIB_PATH=$SCRIPTPATH/../openwrt/staging_dir/target-mips_r2_uClibc-0.9.33.2/usr/lib/:$SOLIB_PATH
SOLIB_PATH=$SCRIPTPATH/../openwrt/staging_dir/target-mips_r2_uClibc-0.9.33.2/root-ar71xx/:$SOLIB_PATH
SOLIB_PATH=$SCRIPTPATH/../openwrt/staging_dir/target-mips_r2_uClibc-0.9.33.2/root-ar71xx/lib/:$SOLIB_PATH
SOLIB_PATH=$SCRIPTPATH/../openwrt/staging_dir/target-mips_r2_uClibc-0.9.33.2/root-ar71xx/lib/lua/:$SOLIB_PATH

# Uncomment this for beep packages built by openwrt
SOLIB_PATH=$SCRIPTPATH/../openwrt/staging_dir/target-mips_r2_uClibc-0.9.33.2/root-ar71xx/beep/:$SOLIB_PATH

# Uncomment this for beep packages built with scons and copied manually
#SOLIB_PATH=$SCRIPTPATH/out/:$SOLIB_PATH

LUA_BIN_PATH=$SCRIPTPATH/../openwrt/staging_dir/target-mips_r2_uClibc-0.9.33.2/root-ar71xx/usr/bin/lua

$MIPS_GDB -ex "set solib-search-path $SOLIB_PATH" $LUA_BIN_PATH $@
