include $(TOPDIR)/rules.mk

PKG_NAME:=beep-base
PKG_RELEASE:=6
PKG_VERSION:=trunk

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

### Core package ###

PKG_BUILD_DEPENDS:=beep

define Package/beep-base
  SECTION:=Beep
  CATEGORY:=Beep
  TITLE:=Beep base files
  URL:=http://test.com
  MAINTAINER:=shlewis@gmail.com
  DEPENDS:=+dropbear
endef

define Package/beep-base/description
 Beep base files that must be installed in the root fs.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
endef

define Build/Configure
endef

define Build/Compile
endef

define Package/beep-base/install
	$(CP) ./files/* $(1)/
endef

$(eval $(call BuildPackage,beep-base))
