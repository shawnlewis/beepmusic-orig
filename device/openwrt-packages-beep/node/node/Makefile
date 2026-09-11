# This is only configured for mipsbe.
include $(TOPDIR)/rules.mk

PKG_NAME:=nodejs
PKG_RELEASE:=1
PKG_VERSION:=0.10.7
PKG_REV:=d2fdae197ac542f686ee06835d1153dd43b862e5

PKG_SOURCE_VERSION:=$(PKG_REV)
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION)-$(PKG_SOURCE_VERSION).tar.gz
PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=git://github.com/joyent/node.git
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

PKG_BUILD_PARALLEL:=1
PKG_INSTALL:=1
PKG_FIXUP:=autoreconf

include $(INCLUDE_DIR)/package.mk

define Package/nodejs
  SECTION:=Beep
  CATEGORY:=Beep
  SUBMENU:=Node
  TITLE:=nodejs for v8
  URL:=https://github.com/joyent/node
  DEPENDS:=+libv8 +librt +libopenssl
endef

define Package/nodejs/description
  nodejs for v8.
endef

NODE_CONFIGURE_OPTS:= \
	AR=$(TARGET_CROSS)ar \
	AS="$(TARGET_CC) -c $(TARGET_CFLAGS)" \
	NM=$(TARGET_CROSS)nm \
	CC="$(TARGET_CC)" \
	GCC="$(TARGET_CC)" \
	CXX="$(TARGET_CXX)" \
	LD="$(TARGET_CXX)" \
	LINK="$(TARGET_CXX)" \
	RANLIB=$(TARGET_CROSS)ranlib \
	STRIP=$(TARGET_CROSS)strip \
	OBJCOPY=$(TARGET_CROSS)objcopy \
	OBJDUMP=$(TARGET_CROSS)objdump \
	SIZE=$(TARGET_CROSS)size

NODE_CONFIGURE:= \
	SHELL="$(BASH)" \
	$(NODE_CONFIGURE_OPTS) \
	$(PKG_BUILD_DIR)/configure \
		$(TARGET_CONFIGURE_ARGS) \
		--without-snapshot \
		--dest-os=linux \
		--dest-cpu=mipseb \
		--without-npm \
		--shared-openssl \
		--shared-v8 \
		--shared-v8-includes=$(STAGING_DIR)/usr/include \
		--shared-v8-libpath=$(STAGING_DIR)/usr/lib

define Build/Configure
	(cd $(PKG_BUILD_DIR); rm -f config.cache; \
		$(NODE_CONFIGURE) \
	);
endef

define Build/Compile
	$(call Build/Compile/Default, \
		PYTHON="/usr/bin/python" \
		LINK.target="$(TARGET_CXX)" \
	)
endef

define Build/Install
	$(INSTALL_DIR) $(PKG_INSTALL_DIR)/usr/bin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/Release/node $(PKG_INSTALL_DIR)/usr/bin/
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/Release/node $(1)/usr/bin/
endef

define Package/nodejs/install
	$(INSTALL_DIR) $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/Release/node $(1)/usr/bin/
endef

$(eval $(call BuildPackage,nodejs))
