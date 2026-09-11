include $(TOPDIR)/rules.mk

PKG_NAME:=beep-first
PKG_RELEASE:=1
PKG_VERSION:=trunk

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

### Core package ###

# NOTE: We specify build dependencies for packages whose files we're
# overriding to ensure that beep-first gets put into the image after those
# packages.
#
# For some reason putting base-files here doesn't get this built after
# base-files, putting beep here gets this built after base-files.
#
# TODO: figure out a full proof way to install beep-first last.
#
PKG_BUILD_DEPENDS:=opkg base-files dropbear beep

define Package/beep-first
  SECTION:=Beep
  CATEGORY:=Beep
  TITLE:=Beep base files
  URL:=http://test.com
  MAINTAINER:=shlewis@gmail.com
endef

define Package/beep-first/description
  Beep files that should be included in an image but not automatically upgraded.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
endef

define Build/Configure
endef

define Build/Compile
endef

define Package/beep-first/install
	$(CP) ./files/* $(1)/
endef

$(eval $(call BuildPackage,beep-first))
