include $(TOPDIR)/rules.mk

PKG_NAME:=libmozjs
PKG_VERSION:=17.0.0
PKG_RELEASE:=1

PKG_SOURCE_URL:=http://ftp.mozilla.org/pub/mozilla.org/js
PKG_SOURCE:=mozjs$(PKG_VERSION).tar.gz
PKG_MD5SUM:=20b6f8f1140ef6e47daa3b16965c9202

PKG_BUILD_DIR:=$(BUILD_DIR)/mozjs$(PKG_VERSION)

PKG_INSTALL:=1
PKG_FIXUP:=autoreconf

include $(INCLUDE_DIR)/package.mk

define Package/libmozjs
  SECTION:=Beep
  CATEGORY:=Beep
  SUBMENU:=Libraries
  TITLE:=Mozilla's JavaScript engine
  URL:=https://developer.mozilla.org/en-US/docs/SpiderMonkey
  DEPENDS:=+zlib +libpthread +libstdcpp
endef

define Package/libmozjs/description
    Mozilla's JavaScript engine
endef

# These can't be empty or they will be replaced with target flags.
CONFIGURE_VARS += \
	HOST_CFLAGS=" " \
	HOST_CXXFLAGS=" " \
	HOST_LDFLAGS=" " \
	PYTHON=/usr/bin/python

# Taken from openwrt/include/package-defaults.mk.  --host needs to use host
# instead of target to compile the build tools correctly.
CONFIGURE_ARGS = \
	--target=$(GNU_TARGET_NAME) \
	--host=$(GNU_HOST_NAME) \
	--build=$(GNU_HOST_NAME) \
	--program-prefix="" \
	--program-suffix="" \
	--prefix=$(CONFIGURE_PREFIX) \
	--exec-prefix=$(CONFIGURE_PREFIX) \
	--bindir=$(CONFIGURE_PREFIX)/bin \
	--sbindir=$(CONFIGURE_PREFIX)/sbin \
	--libexecdir=$(CONFIGURE_PREFIX)/lib \
	--sysconfdir=/etc \
	--datadir=$(CONFIGURE_PREFIX)/share \
	--localstatedir=/var \
	--mandir=$(CONFIGURE_PREFIX)/man \
	--infodir=$(CONFIGURE_PREFIX)/info \
	$(DISABLE_NLS) \
	$(DISABLE_LARGEFILE) \
	$(DISABLE_IPV6)

CONFIGURE_ARGS += \
	--disable-strip \
	--disable-methodjit \
	--with-soft-float=yes \
	--enable-optimize=-O2

CONFIGURE_CMD = ./js/src/configure

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/lib/
	$(INSTALL_DIR) $(1)/usr/include/js-17.0
	$(INSTALL_DIR) $(1)/usr/include/js-17.0/ds
	$(INSTALL_DIR) $(1)/usr/include/js-17.0/gc
	$(INSTALL_DIR) $(1)/usr/include/js-17.0/js
	$(INSTALL_DIR) $(1)/usr/include/js-17.0/mozilla
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/libmozjs-*.so $(1)/usr/lib/
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/dist/include/*.h $(1)/usr/include/js-17.0
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/dist/include/*.tbl $(1)/usr/include/js-17.0
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/dist/include/*.msg $(1)/usr/include/js-17.0
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/dist/include/ds/* $(1)/usr/include/js-17.0/ds
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/dist/include/gc/* $(1)/usr/include/js-17.0/gc
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/dist/include/js/* $(1)/usr/include/js-17.0/js
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/dist/include/mozilla/* $(1)/usr/include/js-17.0/mozilla
endef

define Package/libmozjs/install
	$(INSTALL_DIR) $(1)/usr/lib/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/libmozjs-*.so $(1)/usr/lib/
endef

$(eval $(call BuildPackage,libmozjs))
