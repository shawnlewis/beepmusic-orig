include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=beep-drivers
PKG_RELEASE:=2
PKG_VERSION:=trunk

PKG_BUILD_DIR:=$(KERNEL_BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_USE_MIPS16:=0

include $(INCLUDE_DIR)/package.mk

define KernelPackage/beep-drivers
  SECTION:=Beep
  CATEGORY:=Beep
  TITLE:=Beep Drivers
  URL:=http://test.com
  MAINTAINER:=shawn@beepdevices.com

  FILES:= $(PKG_BUILD_DIR)/ath_i2s.ko
  #AUTOLOAD:=$(call AutoLoad,21,mac80211)
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(TAR) c -C ../../ath_i2s . | \
	        tar x -C $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(MAKE) \
		-C $(PKG_BUILD_DIR) \
		PATH="$(TARGET_PATH)" \
		ARCH="$(LINUX_KARCH)" \
		CROSS_COMPILE="$(TARGET_CROSS)" \
		TOOLPREFIX="$(KERNEL_CROSS)" \
		TOOLPATH="$(KERNEL_CROSS)" \
		KERNELPATH="$(LINUX_DIR)" \
		LDOPTS=" " \
		all
endef

		#TARGET="$(HAL_TARGET)" \

$(eval $(call KernelPackage,beep-drivers))
