#
# Copyright (C) 2009-2012 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#
include $(TOPDIR)/rules.mk

PKG_NAME:=mpc
PKG_VERSION:=0.9
PKG_RELEASE:=1

PKG_SOURCE_URL:=http://www.multiprecision.org/mpc/download/
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz
PKG_MD5SUM:=0d6acab8d214bd7d1fbbc593e83dd00d

PKG_BUILD_PARALLEL:=1
PKG_INSTALL:=1
PKG_FIXUP:=autoreconf

include $(INCLUDE_DIR)/package.mk

define Package/libmpc
  SECTION:=Beep
  CATEGORY:=Beep
  SUBMENU:=Development
  TITLE:=GNU MPC Library
  URL:=http://multiprecision.org/
  DEPENDS:=+libmpfr
endef

define Package/libmpc/description
	GNU MPC Library
endef

CONFIGURE_ARGS += \
	--enable-static \
	--enable-shared \
        EGREP=/bin/egrep

define Build/Compile
	$(call Build/Compile/Default, \
		DESTDIR="$(PKG_INSTALL_DIR)" \
		CC="$(TARGET_CC)" \
		all \
	)
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include
	$(CP) $(PKG_INSTALL_DIR)/usr/include/mpc* $(1)/usr/include/
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libmpc.{a,so*} $(1)/usr/lib/
endef

define Package/libmpc/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libmpc.so.* $(1)/usr/lib/
endef

$(eval $(call BuildPackage,libmpc))
