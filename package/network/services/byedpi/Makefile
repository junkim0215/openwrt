# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk

PKG_NAME:=byedpi
PKG_VERSION:=0.17.3
PKG_RELEASE:=1

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=https://codeload.github.com/hufrea/byedpi/tar.gz/v$(PKG_VERSION)?
PKG_HASH:=0a9cb8585554c68c3e2be88c33c9bf6f99f8e8c7f54b362285adab99e262566c

PKG_LICENSE:=MIT
PKG_LICENSE_FILES:=LICENSE
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/byedpi
  SECTION:=net
  CATEGORY:=Network
  TITLE:=ByeDPI local SOCKS proxy
  URL:=https://github.com/hufrea/byedpi
endef

define Package/byedpi/description
  ByeDPI (ciadpi) is a local SOCKS proxy that splits and modifies the
  first packets of a connection. Off by default; listens on
  127.0.0.1:1080 once enabled in /etc/config/byedpi.
endef

define Package/byedpi/conffiles
/etc/config/byedpi
endef

define Package/byedpi/install
	$(INSTALL_DIR) $(1)/usr/bin $(1)/etc/init.d $(1)/etc/config
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ciadpi $(1)/usr/bin/
	$(INSTALL_BIN) ./files/byedpi.init $(1)/etc/init.d/byedpi
	$(INSTALL_CONF) ./files/byedpi.config $(1)/etc/config/byedpi
endef

$(eval $(call BuildPackage,byedpi))
