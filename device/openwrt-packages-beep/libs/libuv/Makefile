include $(TOPDIR)/rules.mk

PKG_NAME:=libuv
PKG_RELEASE:=1
PKG_VERSION:=v0.10.11
SO_VERSION=0.10
PKG_REV:=c3b75406a66a10222a589cb173e8f469e9665c7e

PKG_SOURCE_VERSION:=$(PKG_REV)
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION)-$(PKG_SOURCE_VERSION).tar.gz
PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=git://github.com/joyent/libuv.git
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

PKG_BUILD_PARALLEL:=1
PKG_INSTALL:=1
PKG_FIXUP:=autoreconf

include $(INCLUDE_DIR)/package.mk

define Package/libuv
  SECTION:=Beep
  CATEGORY:=Beep
  SUBMENU:=Libraries
  TITLE:=Node event driven platform layer
  URL:=https://github.com/joyent/libuv
  DEPENDS:=+librt
endef

define Package/libuv/description
    Node event driven platform layer
endef

define Build/Compile
	$(call Build/Compile/Default, \
		libuv.so \
	)
endef

define Build/Install
	$(INSTALL_DIR) $(PKG_INSTALL_DIR)/usr/lib/
	$(INSTALL_DIR) $(PKG_INSTALL_DIR)/usr/include/
	$(INSTALL_DIR) $(PKG_INSTALL_DIR)/usr/include/uv-private
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/libuv.so $(PKG_INSTALL_DIR)/usr/lib/libuv.so.$(SO_VERSION)
	ln -s libuv.so.$(SO_VERSION) $(PKG_INSTALL_DIR)/usr/lib/libuv.so
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/include/*.h $(PKG_INSTALL_DIR)/usr/include
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/include/uv-private/* $(PKG_INSTALL_DIR)/usr/include/uv-private
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/lib/
	$(INSTALL_DIR) $(1)/usr/include/
	$(INSTALL_DIR) $(1)/usr/include/uv-private
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/libuv.so $(1)/usr/lib/libuv.so.$(SO_VERSION)
	ln -s libuv.so.$(SO_VERSION) $(1)/usr/lib/libuv.so
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/include/*.h $(1)/usr/include
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/include/uv-private/* $(1)/usr/include/uv-private
endef

define Package/libuv/install
	$(INSTALL_DIR) $(1)/usr/lib/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/libuv.so $(1)/usr/lib/libuv.so.$(SO_VERSION)
	ln -s libuv.so.$(SO_VERSION) $(1)/usr/lib/libuv.so
endef

$(eval $(call BuildPackage,libuv))
