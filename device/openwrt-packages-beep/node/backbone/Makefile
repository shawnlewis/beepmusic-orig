include $(TOPDIR)/rules.mk

PKG_NAME:=node-backbone
PKG_VERSION:=1.0.0
PKG_RELEASE:=1

PKG_SOURCE_URL:=http://registry.npmjs.org/backbone/-/
PKG_SOURCE:=backbone-$(PKG_VERSION).tgz
PKG_MD5SUM:=dbe5ee46884dfd4c43486fcd306cefb6

PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

# Need to override the tar command non-standard 'package' prefix.
TAR_CMD=$(HOST_TAR) -C $(1) $(TAR_OPTIONS)

define Package/node-backbone
  SECTION:=Beep
  CATEGORY:=Beep
  SUBMENU:=Node
  TITLE:=backbone package
  URL:=http://backbonejs.org
endef

define Package/node-backbone/description
  backbone package for nodejs.
endef

define Build/Configure
endef

define Build/Compile
endef

define Package/node-backbone/install
	$(INSTALL_DIR) $(1)/usr/lib/node_modules/backbone
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/package/backbone.js $(1)/usr/lib/node_modules/backbone
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/package/index.js $(1)/usr/lib/node_modules/backbone
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/package/package.json $(1)/usr/lib/node_modules/backbone
endef

$(eval $(call BuildPackage,node-backbone))
