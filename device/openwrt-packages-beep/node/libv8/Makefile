# This uses a special version for mipsbe support.  Probably should not be used
# for other targets.
include $(TOPDIR)/rules.mk

PKG_NAME:=libv8
PKG_RELEASE:=beep1
PKG_VERSION:=3.14.5
PKG_REV:=8418fb636b14ac73920ff4de36c7f282ab36a37c

PKG_SOURCE_VERSION:=$(PKG_REV)
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION)-$(PKG_SOURCE_VERSION).tar.gz
PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=git://github.com/v8/v8.git
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

PKG_BUILD_PARALLEL:=1
PKG_INSTALL:=1
PKG_FIXUP:=autoreconf

include $(INCLUDE_DIR)/package.mk

define Package/libv8
  SECTION:=Beep
  CATEGORY:=Beep
  SUBMENU:=Node
  TITLE:=libv8 with mipsbe support
  URL:=https://github.com/v8/v8
  DEPENDS:=+libstdcpp +libpthread
endef

define Package/libv8/description
  libv8 with mipsbe support.
endef

# Node won't link without the debugger.
# -Dv8_enable_debugger_support=0

GYP_FLAGS=-Dv8_use_mips_abi_hardfloat=false \
	-Dv8_can_use_fpu_instructions=false \
	-Dcomponent=shared_library \
	-Dv8_use_snapshot='false' \
	-Dv8_can_use_vfp_instructions=true \
	-Dv8_gc_sections=true

define Build/Prepare
	$(call Build/Prepare/Default)
	$(MAKE) -C $(PKG_BUILD_DIR) dependencies
endef

define Build/Compile
	+LDFLAGS="$(TARGET_LDFLAGS) $(EXTRA_LDFLAGS)" \
	$(MAKE) $(PKG_JOBS) -C $(PKG_BUILD_DIR)/$(MAKE_PATH) \
		$(MAKE_FLAGS) \
		PYTHON="/usr/bin/python" \
		LINK.target="$(TARGET_CXX)" \
		GYPFLAGS="$(GYP_FLAGS)" \
		mips.release;
endef

define Build/Install
	$(INSTALL_DIR) $(PKG_INSTALL_DIR)/usr/lib/
	$(INSTALL_DIR) $(PKG_INSTALL_DIR)/usr/include/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/mips.release/lib.target/libv8.so $(PKG_INSTALL_DIR)/usr/lib/
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/include/* $(PKG_INSTALL_DIR)/usr/include/
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/lib/
	$(INSTALL_DIR) $(1)/usr/include/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/mips.release/lib.target/libv8.so $(1)/usr/lib/
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/include/* $(1)/usr/include/
endef

define Package/libv8/install
	$(INSTALL_DIR) $(1)/usr/lib/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/out/mips.release/lib.target/libv8.so $(1)/usr/lib/
endef

$(eval $(call BuildPackage,libv8))
