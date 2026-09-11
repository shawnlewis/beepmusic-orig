#
# Copyright (C) 2008 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#
include $(TOPDIR)/rules.mk
PKG_NAME:=gcc-native
PKG_RELEASE:=1
GCC_VERSION:=$(call qstrip,$(CONFIG_GCC_VERSION))
PKG_VERSION:=$(firstword $(subst +, ,$(GCC_VERSION)))
GCC_DIR:=$(PKG_NAME)-$(PKG_VERSION)

ifeq ($(findstring linaro, $(CONFIG_GCC_VERSION)),linaro)
    ifeq ($(CONFIG_GCC_VERSION),"4.5-linaro")
      PKG_REV:=4.5-2012.03
      PKG_VERSION:=4.5.4
      PKG_VERSION_MAJOR:=4.5
      PKG_MD5SUM:=0c25f93e15e362e352c933e4649a7fc6
    endif
    ifeq ($(CONFIG_GCC_VERSION),"4.6-linaro")
      PKG_REV:=4.6-2012.12
      PKG_VERSION:=4.6.4
      PKG_VERSION_MAJOR:=4.6
      PKG_MD5SUM:=6b6c6a4faa026edd1193cf6426309039
    endif
    ifeq ($(CONFIG_GCC_VERSION),"4.7-linaro")
      PKG_REV:=4.7-2012.12
      PKG_VERSION:=4.7.3
      PKG_VERSION_MAJOR:=4.7
      PKG_MD5SUM:=247b15687358bc7958eeb91ab22637d1
    endif
    PKG_SOURCE_URL:=http://launchpad.net/gcc-linaro/$(PKG_VERSION_MAJOR)/$(PKG_REV)/+download/
    PKG_SOURCE:=gcc-linaro-$(PKG_REV).tar.bz2
    GCC_DIR:=gcc-linaro-$(PKG_REV)
    PKG_BUILD_DIR:=$(BUILD_DIR)/$(GCC_DIR)
else
  PKG_SOURCE_URL:=@GNU/gcc/gcc-$(PKG_VERSION)
  PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.bz2

  ifeq ($(PKG_VERSION),4.4.7)
    PKG_MD5SUM:=295709feb4441b04e87dea3f1bab4281
  endif
  ifeq ($(PKG_VERSION),4.6.2)
    PKG_MD5SUM:=028115c4fbfb6cfd75d6369f4a90d87e
  endif
  ifeq ($(PKG_VERSION),4.6.3)
    PKG_MD5SUM:=773092fe5194353b02bb0110052a972e
  endif
  ifeq ($(PKG_VERSION),4.7.2)
    PKG_MD5SUM:=cc308a0891e778cfda7a151ab8a6e762
  endif
endif

PKG_INSTALL:=1
PKG_FIXUP:=autoreconf

include $(INCLUDE_DIR)/package.mk

SEP:=,
TARGET_LANGUAGES:="c$(if $(CONFIG_INSTALL_LIBSTDCPP),$(SEP)c++)$(if $(CONFIG_INSTALL_LIBGCJ),$(SEP)java)$(if $(CONFIG_INSTALL_GFORTRAN),$(SEP)fortran)"
EXTRA_TARGET=$(if $(CONFIG_EXTRA_TARGET_ARCH),--enable-targets=$(call qstrip,$(CONFIG_EXTRA_TARGET_ARCH_NAME))-linux-uclibc)

STRIP:=$(STAGING_DIR_HOST)/bin/sstrip
RSTRIP:= \
	NM="$(TARGET_CROSS)nm" \
	STRIP="$(STRIP) --strip-debug" \
	STRIP_KMOD="$(TARGET_CROSS)strip --strip-debug" \
	$(SCRIPT_DIR)/rstrip.sh

define Package/gcc-native
  SECTION:=Beep
  CATEGORY:=Beep
  SUBMENU:=Development
  TITLE:=gcc-native
  DEPENDS:=+binutils +libgmp +libmpfr +libmpc +libpthread
endef

define Package/gcc/description
	build a native toolchain for compiling on target
endef

GCC_CONFIGURE:= \
	SHELL="$(BASH)" \
	$(TARGET_CONFIGURE_OPTS) \
	$(PKG_BUILD_DIR)/configure \
		$(TARGET_CONFIGURE_ARGS) \
		--build=$(GNU_HOST_NAME) \
		--host=$(REAL_GNU_TARGET_NAME) \
		--target=$(REAL_GNU_TARGET_NAME) \
		--enable-languages=$(TARGET_LANGUAGES) \
		--prefix=/usr \
		--libexecdir=/usr/lib \
		--libdir=/usr/lib \
		--includedir=/usr/include \
		--with-gxx-include-dir=/usr/include/c++/$(PKG_VERSION) \
		--enable-shared \
		--disable-__cxa_atexit \
		--enable-target-optspace \
		--with-gnu-ld \
		--disable-nls \
		--disable-libmudflap \
		--disable-multilib \
		$(EXTRA_TARGET) \
		$(SOFT_FLOAT_CONFIG_OPTION) \
		$(call qstrip,$(CONFIG_EXTRA_GCC_CONFIG_OPTIONS)) \

define Build/Prepare
	$(call Build/Prepare/Default)
	(cd $(PKG_BUILD_DIR)/libstdc++-v3; \
	$(SED) 's/m4_rename(/m4_rename_force(/' acinclude.m4;);
endef

define Build/Configure
	(cd $(PKG_BUILD_DIR); rm -f config.cache; \
		$(GCC_CONFIGURE) \
	);
endef

define Build/Compile
	export SHELL="$(BASH)"; $(MAKE_VARS) $(MAKE) -C $(PKG_BUILD_DIR) \
			DESTDIR="$(PKG_INSTALL_DIR)" $(MAKE_ARGS) all install
	# Set up the symlinks to enable lying about target name.
	set -e; \
	(cd $(TOOLCHAIN_DIR); \
		ln -sf $(REAL_GNU_TARGET_NAME) $(GNU_TARGET_NAME); \
		cd bin; \
		for app in $(REAL_GNU_TARGET_NAME)-* ; do \
			ln -sf $$$${app} \
			$(GNU_TARGET_NAME)$$$${app##$(REAL_GNU_TARGET_NAME)}; \
		done; \
	);
endef

define Package/gcc-native/install
	$(INSTALL_DIR) $(1)/usr/lib/
	cp -r $(PKG_INSTALL_DIR)/* $(1)
	cp -r $(TOOLCHAIN_DIR)/include $(1)/usr/
	cp $(TOOLCHAIN_DIR)/lib/crt* $(1)/usr/lib/
	cp $(TOOLCHAIN_DIR)/lib/libc.so $(1)/usr/lib/
endef

$(eval $(call BuildPackage,gcc-native))
