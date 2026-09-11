export BUILD_TOPDIR=$(PWD)
export STAGING_DIR=$(BUILD_TOPDIR)/tmp
export TOPDIR=$(PWD)
export UBOOTDIR=$(TOPDIR)/u-boot

### Toolchain config ###
#buildroot
#CONFIG_TOOLCHAIN_PREFIX=/opt/build/toolchain-mipsbe-4.7.3/bin/mips-linux-

#openwrt NOT YET
CONFIG_TOOLCHAIN_PREFIX=mips-openwrt-linux-uclibc-
export PATH:=$(BUILD_TOPDIR)/../../beep/device/openwrt/staging_dir/toolchain-mips_r2_gcc-4.6-linaro_uClibc-0.9.33.2/bin/:$(PATH)

########################

export CROSS_COMPILE=$(CONFIG_TOOLCHAIN_PREFIX)
export MAKECMD=make ARCH=mips

export UBOOT_GCC_4_3_3_EXTRA_CFLAGS=-fPIC
export BUILD_TYPE=squashfs

export COMPRESSED_UBOOT=0
export FLASH_SIZE=16
export NEW_DDR_TAP_CAL=1
export CONFIG_HORNET_1_1_WAR=1
export MAX_UBOOT_SIZE=256

IMAGEPATH=$(BUILD_TOPDIR)/bin
UBOOTFILE=$(BOARD_TYPE)_u-boot.bin

carambola2:	export BOARD_TYPE=carambola2
carambola2:	export CARABOOT_RELEASE=v2.2-dev
carambola2:	export CONFIG_HORNET_XTAL=40
carambola2:
	make --no-print-directory uboot

beep_cm2:	export BOARD_TYPE=beep_cm2
beep_cm2:	export CONFIG_BEEP_COMMON=1
beep_cm2:	export CONFIG_HORNET_XTAL=40
beep_cm2:
	make --no-print-directory uboot

beep01:	export BOARD_TYPE=beep01
beep01:	export CONFIG_BEEP_COMMON=1
beep01:	export CONFIG_HORNET_XTAL=40
beep01:
	make --no-print-directory uboot

beep_nw64:	export BOARD_TYPE=beep_nw64
beep_nw64:	export CONFIG_BEEP_COMMON=1
beep_nw64:
	make --no-print-directory uboot

uboot:
	cd $(UBOOTDIR) && $(MAKECMD) distclean
	cd $(UBOOTDIR) && $(MAKECMD) $(BOARD_TYPE)_config
	cd $(UBOOTDIR) && $(MAKECMD) all

	mkdir -p $(IMAGEPATH)

ifeq ($(COMPRESSED_UBOOT),1)
	@`cp $(UBOOTDIR)/tuboot.bin $(IMAGEPATH)/temp.bin`
else
	@`cp $(UBOOTDIR)/u-boot.bin $(IMAGEPATH)/temp.bin`
endif
	@echo "**U-Boot image built, raw size:" `wc -c < $(IMAGEPATH)/temp.bin`" bytes"
ifndef CONFIG_SKIP_LOWLEVEL_INIT
	@echo "**Preparing $(MAX_UBOOT_SIZE)KB file filled with 0xFF..."
	@`tr "\000" "\377" < /dev/zero | dd ibs=1k count=$(MAX_UBOOT_SIZE) of=$(IMAGEPATH)/$(UBOOTFILE) 2> /dev/null`
	@echo "**Copying U-Boot image..."
	@`dd if=$(IMAGEPATH)/temp.bin of=$(IMAGEPATH)/$(UBOOTFILE) conv=notrunc 2> /dev/null`
	@`rm $(BUILD_TOPDIR)/bin/temp.bin`
else
	@echo "**Copying U-Boot image..."
	@`mv $(IMAGEPATH)/temp.bin $(IMAGEPATH)/$(UBOOTFILE)`
endif
	@echo "**U-Boot image ready, size:" `wc -c < $(IMAGEPATH)/$(UBOOTFILE)`" bytes"
	@`md5sum $(IMAGEPATH)/$(UBOOTFILE) | awk '{print $$1}' | tr -d '\n' > $(IMAGEPATH)/$(UBOOTFILE).md5sum`
# Do not check image size for RAM version
ifndef CONFIG_SKIP_LOWLEVEL_INIT
	@if [ "`wc -c < $(IMAGEPATH)/$(UBOOTFILE)`" -gt "`/bin/echo '$(MAX_UBOOT_SIZE)*1024' | bc`" ]; then \
			/bin/echo -e "\n**************************************************"; \
			/bin/echo "*     WARNING: U-BOOT IMAGE SIZE IS TOO BIG!     *"; \
			/bin/echo -e "**************************************************"; \
	fi;
endif

clean:
	cd $(UBOOTDIR) && $(MAKECMD) distclean
