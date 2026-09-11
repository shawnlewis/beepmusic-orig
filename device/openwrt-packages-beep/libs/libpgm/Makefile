include $(TOPDIR)/rules.mk

PKG_NAME:=libpgm
LIBPGM_VERSION:=5.2.122
PKG_RELEASE:=1
PKG_VERSION:=beep-$(PKG_RELEASE)-$(LIBPGM_VERSION)

PKG_SOURCE_URL:=http://openpgm.googlecode.com/files
PKG_SOURCE:=$(PKG_NAME)-$(LIBPGM_VERSION).tar.gz
PKG_MD5SUM:=d7673e9ff6cc33cf42fe9fb9a7bfbffa

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(LIBPGM_VERSION)

PKG_BUILD_PARALLEL:=1
PKG_INSTALL:=1
PKG_FIXUP:=autoreconf

include $(INCLUDE_DIR)/package.mk

define Package/libpgm
  SECTION:=Beep
  CATEGORY:=Beep
  SUBMENU:=Libraries
  TITLE:=OpenPGM reliable mulitcast library
  URL:=https://code.google.com/p/openpgm
  DEPENDS:= +libpthread
endef

define Package/libpgm/description
    OpenPGM reliable multicast library
endef

PKG_CONFIGURE_OPTS:= \
	ac_cv_file__proc_cpuinfo=yes \
	ac_cv_file__dev_rtc=no \
	ac_cv_file__dev_hpet=no

EXTRA_CFLAGS += \
    -std=gnu99

define Build/Configure
	(cd $(PKG_BUILD_DIR)/openpgm/pgm; autoreconf --force --install || exit 0)
	$(call Build/Configure/Default, \
		$(PKG_CONFIGURE_OPTS) \
		,,openpgm/pgm \
	)
endef

MAKE_PATH=openpgm/pgm

define Build/Compile
	$(call Build/Compile/Default,)
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_BUILD_DIR)/openpgm/pgm/.libs/libpgm*.so* $(1)/usr/lib/
	$(CP) $(PKG_BUILD_DIR)/openpgm/pgm/.libs/libpgm.la $(1)/usr/lib/
	$(INSTALL_DIR) $(1)/usr/include
	$(CP) -r $(PKG_BUILD_DIR)/openpgm/pgm/include/pgm $(1)/usr/include/
endef

define Package/libpgm/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_BUILD_DIR)/openpgm/pgm/.libs/libpgm*.so* $(1)/usr/lib/
endef

$(eval $(call BuildPackage,libpgm))
