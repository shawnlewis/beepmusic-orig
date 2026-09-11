include $(TOPDIR)/rules.mk

PKG_NAME:=beep-collectd-mod
PKG_RELEASE:=1
PKG_VERSION:=trunk

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/beep-collectd-mod-smaps
  SECTION:=Beep
  CATEGORY:=Beep
  SUBMENU:=Utilities
  TITLE:=Beep collectd smaps module
  URL:=http://beepdevices.com
  MAINTAINER:=joe@beepdevices.com
  DEPENDS:=+collectd
endef

define Package/beep-collectd-mod-smaps/description
  Beep collectd smaps module
endef

define Package/beep-collectd-mod-ubusmon
  SECTION:=Beep
  CATEGORY:=Beep
  SUBMENU:=Utilities
  TITLE:=Beep collectd ubus monitor module
  URL:=http://beepdevices.com
  MAINTAINER:=joe@beepdevices.com
  DEPENDS:=+collectd +libubox +libubus
endef

define Package/beep-collectd-mod-ubusmon/description
  Beep collectd ubus monitor module
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

define Package/beep-collectd-mod-smaps/install
	$(INSTALL_DIR) $(1)/usr/lib/collectd
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/smaps.so $(1)/usr/lib/collectd
endef

define Package/beep-collectd-mod-ubusmon/install
	$(INSTALL_DIR) $(1)/usr/lib/collectd
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ubusmon.so $(1)/usr/lib/collectd
endef

$(eval $(call BuildPackage,beep-collectd-mod-smaps))
$(eval $(call BuildPackage,beep-collectd-mod-ubusmon))
