include $(TOPDIR)/rules.mk

PKG_NAME:=amnezia-xray-core
PKG_VERSION:=26.7.28
PKG_RELEASE:=4

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/amnezia-vpn/amnezia-xray-core.git
PKG_SOURCE_DATE:=2026-09-24
PKG_SOURCE_VERSION:=77e5d55a796bafe17c1b5f2a9d9c590fd2a9f5f3
PKG_MIRROR_HASH:=skip

PKG_LICENSE:=MPL-2.0
PKG_LICENSE_FILES:=LICENSE

PKG_BUILD_DEPENDS:=golang/host
PKG_BUILD_PARALLEL:=1
PKG_BUILD_FLAGS:=no-mips16

GO_PKG:=github.com/xtls/xray-core
GO_PKG_BUILD_PKG:=github.com/xtls/xray-core/main
GO_PKG_LDFLAGS_X:= \
	$(GO_PKG)/core.build=OpenWrt \
	$(GO_PKG)/core.version=$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk
include $(TOPDIR)/feeds/packages/lang/golang/golang-package.mk

# x/crypto has no 32-bit ARM code for ChaCha20-Poly1305: ChaCha20 is generic
# Go and Poly1305 uses 64-bit limbs (bits.Mul64: four multiplies on 32-bit
# ARM). Build against a copy of the module with ARM assembly for ChaCha20 and
# a 26-bit-limb Poly1305 for GOARCH=arm when the version matches: the AEAD
# gets ~65% faster on the AN7563.
XCRYPTO_VERSION:=v0.54.0

define Build/Compile
	cd $(PKG_BUILD_DIR) && \
	if grep -q 'golang.org/x/crypto $(XCRYPTO_VERSION)$$$$' go.mod; then \
		$(GO_GENERAL_BUILD_CONFIG_VARS) $(GO_PKG_BUILD_CONFIG_VARS) $(GO_PKG_VARS) \
			go mod download golang.org/x/crypto && \
		rm -rf third_party/x-crypto && mkdir -p third_party && \
		cp -r $(GO_MOD_CACHE_DIR)/golang.org/x/crypto@$(XCRYPTO_VERSION) third_party/x-crypto && \
		chmod -R u+w third_party/x-crypto && \
		cp -r $(CURDIR)/xcrypto/. third_party/x-crypto/ && \
		$(GO_GENERAL_BUILD_CONFIG_VARS) $(GO_PKG_BUILD_CONFIG_VARS) $(GO_PKG_VARS) \
			go mod edit -replace golang.org/x/crypto=./third_party/x-crypto; \
	fi
	$(call GoPackage/Build/Compile)
endef

define Package/amnezia-xray-core
  TITLE:=Xray with the Amnezia server extensions
  SECTION:=net
  CATEGORY:=Network
  URL:=https://github.com/amnezia-vpn/amnezia-xray-core
  DEPENDS:=$(GO_ARCH_DEPENDS) +ca-bundle
  PROVIDES:=xray-core
  CONFLICTS:=xray-core
endef

define Package/amnezia-xray-core/description
  Xray built from the Amnezia fork, which adds the xhttp padding and
  placement options its servers use (xPaddingHeader, seqPlacement,
  sessionIDKey, uplinkHTTPMethod and others).
endef

define Package/amnezia-xray-core/conffiles
/etc/xray/
/etc/config/xray
endef

define Package/amnezia-xray-core/install
	$(call GoPackage/Package/Install/Bin,$(PKG_INSTALL_DIR))
	$(INSTALL_DIR) $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/main $(1)/usr/bin/xray

	$(INSTALL_DIR) $(1)/etc/xray/
	$(INSTALL_DATA) ./files/config.json.example $(1)/etc/xray/

	$(INSTALL_DIR) $(1)/etc/config/
	$(INSTALL_CONF) ./files/xray.conf $(1)/etc/config/xray
	$(INSTALL_DIR) $(1)/etc/init.d/
	$(INSTALL_BIN) ./files/xray.init $(1)/etc/init.d/xray
endef

$(eval $(call BuildPackage,amnezia-xray-core))
