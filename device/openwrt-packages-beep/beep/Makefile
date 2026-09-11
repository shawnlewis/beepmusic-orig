include $(TOPDIR)/rules.mk

PKG_NAME:=beep
PKG_RELEASE:=2
PKG_VERSION:=trunk

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

PKG_BUILD_DETERMINISTIC:=0

include $(INCLUDE_DIR)/package.mk


### Default ###
define Package/beep-default/Default
  SECTION:=Beep
  CATEGORY:=Beep
  URL:=http://thisisbeep.com
  MAINTAINER:=joe@thisisbeep.com
endef


### Config package ###
define Package/beep-config
$(call Package/beep-default/Default)
  TITLE:=Beep build config
  MENU:=1
  DEPENDS:=
endef

define Package/beep-config/description
    Beep build configuration.
endef

define Package/beep-config/config
	source "$(SOURCE)/Config.in"
endef

# Without this the build system will always warn about unmet dependencies.
define Package/beep-config/install
	$(INSTALL_DIR) $(1)/beep
endef

BEEP_CONFIG_ARGS:=

ifdef CONFIG_BEEP_COMPILE_LUA
  BEEP_CONFIG_ARGS+= --compile-lua
endif

### Package prepare/configure/compile ###
# Force using system scons instead of OpenWrt scons (we require 2.3.0).
# OpenWrt's trunk currently uses 2.3.0.
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	sh -c "rm -f ../../src/.binver_cache"
	sh -c "cd ../../src; /usr/bin/scons --target=$(CONFIG_BEEP_TARGET) clean"
	sh -c "cd ../../src; /usr/bin/scons --binver-cache=store"
	$(TAR) c -C ../../src . | \
		tar x -C $(PKG_BUILD_DIR)/
	$(CP) ../../../VERSION $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

define Build/Compile
	+$(MAKE_VARS) \
	$(TARGET_CONFIGURE_OPTS) \
	/usr/bin/scons $(PKG_JOBS) -C $(PKG_BUILD_DIR) \
		--target=$(CONFIG_BEEP_TARGET) $(BEEP_CONFIG_ARGS) \
		--binver-cache=yes --toolchain=buildroot --verbose;
endef


### Beep update ###
define Package/beepupdate
$(call Package/beep-default/Default)
  TITLE:=Beep update
  DEPENDS:= \
	+beep-config \
	+libcurl \
	+libixml \
	+libopenssl \
	+librt \
	+libuci \
	+libupnp \
	+libyaml \
	+zlib
endef

define Package/beepupdate/description
  Beep update
endef

define Package/beepupdate/install
	$(INSTALL_DIR) $(1)/beep
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/beepupdate $(1)/beep
endef


### Beep core uploader ###
define Package/bcore
$(call Package/beep-default/Default)
  TITLE:=Beep core uploader
  DEPENDS:= \
	+beep-config \
	+libcurl \
	+libuci
endef

define Package/bcore/description
  Beep core uploader
endef

define Package/bcore/install
	$(INSTALL_DIR) $(1)/beep
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/bcore $(1)/beep
endef


### libbeep ###
define Package/libbeep
$(call Package/beep-default/Default)
  SUBMENU:=Beep Libraries
  TITLE:=libbeep.so
  DEPENDS:= \
	+beep-config \
	+libblobmsg-json \
	+libcurl \
	+libjson \
	+libpthread \
	+libubox \
	+libubus \
	+libuci \
	+mdns-utils
endef

define Package/libbeep/description
  libbeep.so.
endef

define Package/libbeep/install
	$(INSTALL_DIR) $(1)/beep
	$(INSTALL_DIR) $(1)/beep/platform
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/libbeep.so $(1)/beep/platform
endef


### libbeepnl ###
define Package/libbeepnl
$(call Package/beep-default/Default)
  SUBMENU:=Beep Libraries
  TITLE:=libbeepnl.so
  DEPENDS:= \
	+beep-config \
	+libbeep \
	+libnl-tiny
endef

define Package/libbeepnl/description
  libbeepnl.so.
endef

define Package/libbeepnl/install
	$(INSTALL_DIR) $(1)/beep
	$(INSTALL_DIR) $(1)/beep/platform
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/libbeepnl.so $(1)/beep/platform
endef


### Wifisetup ###
define Package/wifisetup
$(call Package/beep-default/Default)
  TITLE:=wifisetup
  DEPENDS:= \
	+beep-config \
	+libbeep \
	+libbeepnl
endef

define Package/wifisetup/description
  wifisetup
endef

define Package/wifisetup/install
	$(INSTALL_DIR) $(1)/beep
	$(INSTALL_DIR) $(1)/beep/platform
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/wifisetup $(1)/beep/platform
endef


### Core package ###
define Package/beep
$(call Package/beep-default/Default)
  TITLE:=Beep core platform
  DEPENDS:= \
	+beep-config \
	+beepupdate \
	+bcore \
	+libbeep \
	+libbeepnl \
	+wifisetup \
	+libblobmsg-json \
	+libc \
	+libcurl \
	+libfdk-aac \
	+libflac \
	+libjson \
	+liblua \
	+libmad \
	+libmozjs \
	+libnl-tiny \
	+libopenssl \
	+libpthread \
	+librt \
	+libubox \
	+libubus \
	+libuci \
	+libuuid \
	+libuv \
	+libvorbisidec \
	+mdns-utils
endef

define Package/beep/description
  Beep is beepy.
endef

define Package/beep/install
    echo "Installing!"

	$(INSTALL_DIR) $(1)/beep
	$(INSTALL_DIR) $(1)/beep/platform

	$(INSTALL_DATA) $(PKG_BUILD_DIR)/VERSION $(1)/beep/platform/VERSION

	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/app_generic $(1)/beep/platform
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/beepcloud $(1)/beep/platform
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/beepcomm $(1)/beep/platform
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/beepdiscovery $(1)/beep/platform
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/beephead_simple $(1)/beep/platform
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/beepi2s $(1)/beep/platform
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/gmrender $(1)/beep/platform
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/playnet $(1)/beep/platform
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/pseudorandomsleep $(1)/beep
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/urelay $(1)/beep/platform

	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/libi2c.so $(1)/beep/platform
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/libmdns.so $(1)/beep/platform

	$(INSTALL_DIR) $(1)/beep/platform/lualib

	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lualib/beep.so $(1)/beep/platform/lualib
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lualib/i2c.so $(1)/beep/platform/lualib
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lualib/gpio.so $(1)/beep/platform/lualib
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lualib/stream.so $(1)/beep/platform/lualib
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lualib/beepnl.so $(1)/beep/platform/lualib
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lualib/crypto.so $(1)/beep/platform/lualib
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lualib/trigint.so $(1)/beep/platform/lualib
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lualib/curl.so $(1)/beep/platform/lualib

	$(INSTALL_DIR) $(1)/beep/platform/lua

	# These may be real files if --compile-lua is used, if not they will be symlinks.

	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepio_bus.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepio_clock.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepio_comm_beepdial.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepio_comm_gpio.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepio_comm_i2c.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepio_comm_ubus.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepio_config.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepio_config_run.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepio_controller.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepio_input_action.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepio_input.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepio_input_triggers.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepio.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepio_model.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepio_model_run.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepio_output.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepio_output_views.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepio_registry.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepio_wifisetup.lua $(1)/beep/platform/lua

	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beephealth.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beephead.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beephead_remotes.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepmanager.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepmanager_apps.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepmanager_device.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepmanager_grouper.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepmanager_grouping.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepmanager_netcheck.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepmanager_networking.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepmanager_subscriber.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/beepmanager_updates.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/distributor.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/distributor_players.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/distributor_history.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/app_playfile.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/app_testtarget.lua $(1)/beep/platform/lua
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/nest.lua $(1)/beep/platform/lua

	$(INSTALL_DIR) $(1)/beep/platform/lua/lib

	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/lib/agent_manager.lua $(1)/beep/platform/lua/lib
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/lib/async.lua $(1)/beep/platform/lua/lib
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/lib/bdebug.lua $(1)/beep/platform/lua/lib
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/lib/beepcloud.lua $(1)/beep/platform/lua/lib
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/lib/beep_player.lua $(1)/beep/platform/lua/lib
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/lib/beep_ubus.lua $(1)/beep/platform/lua/lib
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/lib/config.lua $(1)/beep/platform/lua/lib
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/lib/device.lua $(1)/beep/platform/lua/lib
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/lib/flags.lua $(1)/beep/platform/lua/lib
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/lib/inspect.lua $(1)/beep/platform/lua/lib
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/lib/JSON.lua $(1)/beep/platform/lua/lib
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/lib/log.lua $(1)/beep/platform/lua/lib
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/lib/replay_log.lua $(1)/beep/platform/lua/lib
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/lib/strict.lua $(1)/beep/platform/lua/lib
	$(CP) -L $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/lua/lib/util.lua $(1)/beep/platform/lua/lib


	# These are symlinks in the build, so follow them with -L
	$(CP) -rL $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/static $(1)/beep/platform

	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/beepjs $(1)/beep/platform
	$(CP) -rL $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/js $(1)/beep/platform

endef


### Beep tools ###
define Package/beep-tools
$(call Package/beep-default/Default)
  TITLE:=Beep tools
  DEPENDS:= \
	+beep-config
endef

define Package/beep-tools/description
  Beep tools.
endef

define Package/beep-tools/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/burn $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/devmem $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/freemem $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/athplay $(1)/usr/bin
endef


### Beep tests ###
define Package/beep-test
$(call Package/beep-default/Default)
  TITLE:=Beep test programs
  DEPENDS:= \
	+beep-config \
	+beep \
	+check
endef

define Package/beep-test/description
  Beep tests.
endef

define Package/beep-test/install
	$(INSTALL_DIR) $(1)/beep
	$(INSTALL_DIR) $(1)/beep/test
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/$(CONFIG_BEEP_TARGET)/beep_flags_test $(1)/beep/test
endef


$(eval $(call BuildPackage,beep-config))
$(eval $(call BuildPackage,beepupdate))
$(eval $(call BuildPackage,bcore))
$(eval $(call BuildPackage,libbeep))
$(eval $(call BuildPackage,libbeepnl))
$(eval $(call BuildPackage,wifisetup))
$(eval $(call BuildPackage,beep))
$(eval $(call BuildPackage,beep-tools))
$(eval $(call BuildPackage,beep-test))
