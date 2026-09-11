include $(TOPDIR)/rules.mk

PKG_NAME:=node-uuid
PKG_VERSION:=1.4.0
PKG_RELEASE:=1

PKG_SOURCE_URL:=http://registry.npmjs.org/node-uuid/-/
PKG_SOURCE:=node-uuid-$(PKG_VERSION).tgz
PKG_MD5SUM:=25074edcf3aa674304e74fe81bbe9b66

PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

# Need to override the tar command non-standard 'package' prefix.
TAR_CMD=$(HOST_TAR) -C $(1) $(TAR_OPTIONS)

define Package/node-uuid
  SECTION:=Beep
  CATEGORY:=Beep
  SUBMENU:=Node
  TITLE:=node-uuid package
  URL:=http://github.com/broofa/node-uuid
endef

define Package/node-uuid/description
  node-uuid package for nodejs.
endef

define Build/Configure
endef

define Build/Compile
endef

define Package/node-uuid/install
	$(INSTALL_DIR) $(1)/usr/lib/node_modules/node-uuid
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/package/package.json $(1)/usr/lib/node_modules/node-uuid
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/package/uuid.js $(1)/usr/lib/node_modules/node-uuid
endef

$(eval $(call BuildPackage,node-uuid))
