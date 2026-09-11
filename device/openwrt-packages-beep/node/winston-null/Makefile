include $(TOPDIR)/rules.mk

PKG_NAME:=node-winston-null
PKG_RELEASE:=1
PKG_VERSION:=trunk

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/node-winston-null
  SECTION:=Beep
  CATEGORY:=Beep
  SUBMENU:=Node
  TITLE:=winston-null package
  URL:=http://beepdevices.com
  MAINTAINER:=joe@beepdevices.com
endef

define Package/node-winston-null/description
  winston-null package for nodejs.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
endef

define Build/Configure
endef

define Build/Compile
endef

define Package/node-winston-null/install
	$(INSTALL_DIR) $(1)/usr/lib/node_modules/winston
	$(INSTALL_DATA) ./files/index.js $(1)/usr/lib/node_modules/winston
	$(INSTALL_DATA) ./files/package.json $(1)/usr/lib/node_modules/winston
endef

$(eval $(call BuildPackage,node-winston-null))
