include $(TOPDIR)/rules.mk

PKG_NAME:=fdk-aac
FDK_AAC_VERSION:=0.1.2
PKG_RELEASE:=1
PKG_VERSION:=beep-$(PKG_RELEASE)-$(FDK_AAC_VERSION)

PKG_SOURCE_URL:=@SF/opencore-amr/$(PKG_NAME)
PKG_SOURCE:=$(PKG_NAME)-$(FDK_AAC_VERSION).tar.gz
PKG_MD5SUM:=50a6f0a7830b51185f5dffda943c9062

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(FDK_AAC_VERSION)

PKG_BUILD_PARALLEL:=1
PKG_INSTALL:=1
PKG_FIXUP:=autoreconf
PKG_USE_MIPS16:=0

include $(INCLUDE_DIR)/package.mk

define Package/libfdk-aac
  SECTION:=Beep
  CATEGORY:=Beep
  SUBMENU:=Libraries
  TITLE:=OpenCORE AAC codec
  URL:=http://sourceforge.net/projects/opencore-amr/
  DEPENDS:=
endef

define Package/libfdk-aac/description
    OpenCORE AAC codec
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/lib/
	$(INSTALL_DIR) $(1)/usr/include/fdk-aac
	$(CP) $(PKG_BUILD_DIR)/.libs/libfdk-aac.so* $(1)/usr/lib
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/libAACdec/include/aacdecoder_lib.h $(1)/usr/include/fdk-aac/
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/libAACenc/include/aacenc_lib.h $(1)/usr/include/fdk-aac/
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/libSYS/include/FDK_audio.h $(1)/usr/include/fdk-aac/
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/libSYS/include/genericStds.h $(1)/usr/include/fdk-aac/
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/libSYS/include/machine_type.h $(1)/usr/include/fdk-aac/
endef

define Package/libfdk-aac/install
	$(INSTALL_DIR) $(1)/usr/lib/
	$(INSTALL_DIR) $(1)/usr/include/fdk-aac
	$(CP) $(PKG_BUILD_DIR)/.libs/libfdk-aac.so* $(1)/usr/lib
endef

$(eval $(call BuildPackage,libfdk-aac))
