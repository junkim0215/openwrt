# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-amneziawg-menu
PKG_VERSION:=1
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-only

LUCI_TITLE:=LuCI menu entry for AmneziaWG
LUCI_DESCRIPTION:=Services → AmneziaWG: lists the AmneziaWG interfaces and creates a new one
LUCI_DEPENDS:=+luci-base +luci-proto-amneziawg
LUCI_PKGARCH:=all

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot signature
