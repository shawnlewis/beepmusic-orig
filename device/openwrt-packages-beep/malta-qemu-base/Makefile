include $(TOPDIR)/rules.mk

PKG_NAME:=malta-qemu-base
PKG_RELEASE:=1
PKG_VERSION:=trunk

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/malta-qemu-base
  SECTION:=Beep
  CATEGORY:=Beep
  TITLE:=Malta qemu base files
  URL:=http://beepdevices.com
  MAINTAINER:=joe@beepdevices.com
  DEPENDS:=+opkg
endef

define Package/malta-qemu-base/description
  Files for malta running under qemu.
endef

define Package/malta-qemu-base/conffiles
/etc/opkg.conf
/etc/config/fstab
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
endef

define Build/Configure
endef

define Build/Compile
endef

define Package/malta-qemu-base/install
	$(INSTALL_DIR) $(1)/etc
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_DATA) ./files/etc/opkg.conf $(1)/etc/
	$(INSTALL_DATA) ./files/etc/config/fstab $(1)/etc/config/
endef

$(eval $(call BuildPackage,malta-qemu-base))
