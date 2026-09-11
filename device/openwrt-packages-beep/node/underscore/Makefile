include $(TOPDIR)/rules.mk

PKG_NAME:=node-underscore
PKG_VERSION:=1.4.4
PKG_RELEASE:=1

PKG_SOURCE_URL:=http://registry.npmjs.org/underscore/-/
PKG_SOURCE:=underscore-$(PKG_VERSION).tgz
PKG_MD5SUM:=b3f32b8c28916abc21eed67d0d7f91c8

PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

# Need to override the tar command non-standard 'package' prefix.
TAR_CMD=$(HOST_TAR) -C $(1) $(TAR_OPTIONS)

define Package/node-underscore
  SECTION:=Beep
  CATEGORY:=Beep
  SUBMENU:=Node
  TITLE:=underscore package
  URL:=http://underscorejs.org
endef

define Package/node-underscore/description
  underscore package for nodejs.
endef

define Build/Configure
endef

define Build/Compile
endef

define Package/node-underscore/install
	$(INSTALL_DIR) $(1)/usr/lib/node_modules/underscore
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/package/underscore.js $(1)/usr/lib/node_modules/underscore
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/package/index.js $(1)/usr/lib/node_modules/underscore
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/package/package.json $(1)/usr/lib/node_modules/underscore
endef

$(eval $(call BuildPackage,node-underscore))
