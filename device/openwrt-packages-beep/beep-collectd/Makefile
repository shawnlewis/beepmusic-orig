include $(TOPDIR)/rules.mk

PKG_NAME:=beep-collectd
PKG_RELEASE:=1
PKG_VERSION:=trunk

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/beep-collectd
  SECTION:=Beep
  CATEGORY:=Beep
  TITLE:=Beep collectd settings
  URL:=http://beepdevices.com
  MAINTAINER:=joe@beepdevices.com
  DEPENDS:=+collectd \
	+collectd-mod-cpu \
	+collectd-mod-memory \
	+collectd-mod-syslog \
	+collectd-mod-write-graphite \
	+collectd-mod-wireless \
	+collectd-mod-interface
endef

define Package/beep-collectd/description
  Beep collectd settings
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
endef

define Build/Configure
endef

define Build/Compile
endef

define Package/beep-collectd/install
	$(INSTALL_DIR) $(1)/etc
	$(INSTALL_CONF) ./files/etc/collectd.conf $(1)/etc
endef

$(eval $(call BuildPackage,beep-collectd))
