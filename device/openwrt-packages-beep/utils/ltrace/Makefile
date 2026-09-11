include $(TOPDIR)/rules.mk

PKG_NAME:=ltrace
PKG_VERSION:=0.7.3
PKG_RELEASE:=1
PKG_REV:=37ecc41b58be3dbdd79592a76e331b5b371e4f81

PKG_SOURCE_VERSION:=$(PKG_REV)
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION)-$(PKG_SOURCE_VERSION).tar.gz
PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=git://git.debian.org/git/collab-maint/ltrace.git
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)

PKG_BUILD_PARALLEL:=1
PKG_INSTALL:=1
PKG_FIXUP:=autoreconf

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/ltrace
  SECTION:=Beep
  CATEGORY:=Beep
  SUBMENU:=Utilities
  TITLE:=Library call tracer
  URL:=http://www.ltrace.org/
  DEPENDS:=+libelf1 +libstdcpp
endef

define Package/ltrace/description
	Library call tracer
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/etc
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/etc/ltrace.conf $(1)/etc
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ltrace $(1)/usr/bin
endef

define Package/ltrace/install
	$(INSTALL_DIR) $(1)/etc
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/etc/ltrace.conf $(1)/etc
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ltrace $(1)/usr/bin
endef

$(eval $(call BuildPackage,ltrace))
