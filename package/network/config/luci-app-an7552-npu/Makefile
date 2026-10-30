# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-an7552-npu
PKG_VERSION:=1
PKG_RELEASE:=7
PKG_LICENSE:=GPL-2.0-only

LUCI_TITLE:=LuCI switch for the AN7552 Wi-Fi NPU offload
LUCI_DESCRIPTION:=Turns the AN7552 NPU Wi-Fi offload of the mt7996e driver on or off
LUCI_DEPENDS:=+luci-base +kmod-mt7996e
LUCI_PKGARCH:=all

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot signature
