# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-be5000-temp
PKG_VERSION:=1
PKG_RELEASE:=2
PKG_LICENSE:=GPL-2.0-only

LUCI_TITLE:=LuCI temperature status for the Xiaomi BE5000
LUCI_DESCRIPTION:=Shows the CPU, SoC and Wi-Fi temperatures on the status page
LUCI_DEPENDS:=+luci-base
LUCI_PKGARCH:=all

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot signature
