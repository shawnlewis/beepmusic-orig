include $(TOPDIR)/rules.mk

PKG_NAME:=bones
PKG_RELEASE:=1
PKG_VERSION:=trunk

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/bones
  SECTION:=Beep
  CATEGORY:=Beep
  TITLE:=Bones crash reporter
  URL:=http://beepdevices.com
  MAINTAINER:=joe@beepdevices.com
  DEPENDS:=+base-files +bcore
endef

define Package/bones/description
  Bones crash reporter enabled on boot.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
endef

define Build/Configure
endef

define Build/Compile
endef

define Package/bones/install
	$(INSTALL_DIR) $(1)/beep
	$(INSTALL_DIR) $(1)/etc
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_DIR) $(1)/etc/profile.d
	$(INSTALL_BIN) ./files/beep/bones.sh $(1)/beep
	$(INSTALL_BIN) ./files/etc/profile.d/bones $(1)/etc/profile.d
	$(INSTALL_BIN) ./files/etc/init.d/bones $(1)/etc/init.d
endef

$(eval $(call BuildPackage,bones))
