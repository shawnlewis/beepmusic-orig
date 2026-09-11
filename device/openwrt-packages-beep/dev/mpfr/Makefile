#
# Copyright (C) 2009 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#
include $(TOPDIR)/rules.mk

PKG_NAME:=mpfr
PKG_VERSION:=3.0.0
PKG_RELEASE:=1

PKG_SOURCE_URL:=http://www.mpfr.org/mpfr-$(PKG_VERSION)
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.bz2
PKG_MD5SUM:=f45bac3584922c8004a10060ab1a8f9f

PKG_BUILD_PARALLEL:=1
PKG_INSTALL:=1
PKG_FIXUP:=autoreconf

include $(INCLUDE_DIR)/package.mk

define Package/libmpfr
  SECTION:=Beep
  CATEGORY:=Beep
  SUBMENU:=Development
  TITLE:=multiple-precision floating-point library
  URL:=http://www.mpfr.org/
  DEPENDS:=+libgmp
endef

define Package/libmpfr/description
	multiple-precision floating-point library
endef

CONFIGURE_ARGS += \
	--enable-static \
	--enable-shared

define Build/Compile
	$(call Build/Compile/Default, \
		DESTDIR="$(PKG_INSTALL_DIR)" \
		CC="$(TARGET_CC)" \
		all \
	)
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include
	$(CP) $(PKG_INSTALL_DIR)/usr/include/mpfr* $(1)/usr/include/
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libmpfr.{a,so*} $(1)/usr/lib/
endef

define Package/libmpfr/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libmpfr.so.* $(1)/usr/lib/
endef

$(eval $(call BuildPackage,libmpfr))
