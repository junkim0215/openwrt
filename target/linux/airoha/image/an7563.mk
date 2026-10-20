define Target/Description
	Build firmware images for Airoha AN7563 (ARMv8 Cortex-A53 running
	in AArch32 mode) based boards.
endef

define Build/an7563-uboot
  cat $(STAGING_DIR_IMAGE)/an7563_$1-u-boot.bin >> $@
endef

define Device/airoha_an7563-evb
  DEVICE_VENDOR := Airoha
  DEVICE_MODEL := AN7563 Evaluation Board
  DEVICE_DTS := an7563-evb
  KERNEL_LOADADDR := 0x80088000
  ARTIFACT/u-boot.bin := an7563-uboot rfb
  ARTIFACTS := u-boot.bin
endef
TARGET_DEVICES += airoha_an7563-evb

define Device/xiaomi_be5000
  DEVICE_VENDOR := Xiaomi
  DEVICE_MODEL := BE5000
  DEVICE_DTS := an7563-xiaomi-be5000
  DEVICE_PACKAGES := an7552-npu-firmware kmod-phy-airoha-en8811h \
	kmod-mt7996e kmod-mt7992-23-firmware wpad-mbedtls iw nand-utils \
	luci-app-an7552-npu luci-i18n-an7552-npu-ru \
	luci-i18n-base-ru luci-i18n-firewall-ru luci-i18n-package-manager-ru
  KERNEL_LOADADDR := 0x80088000
  IMAGE_SIZE := 24576k
  IMAGE/sysupgrade.bin := append-kernel | pad-to 128k | append-rootfs | \
	pad-rootfs | append-metadata | check-size
  ARTIFACT/u-boot.bin := an7563-uboot xiaomi_be5000
  ARTIFACTS := u-boot.bin
endef
TARGET_DEVICES += xiaomi_be5000

define Device/xiaomi_be5000-full
  $(Device/xiaomi_be5000)
  DEVICE_VARIANT := (VPN)
  DEVICE_PACKAGES += kmod-amneziawg amneziawg-tools luci-proto-amneziawg \
	luci-i18n-amneziawg-ru zapret luci-app-zapret \
	https-dns-proxy luci-app-https-dns-proxy luci-i18n-https-dns-proxy-ru \
	pbr luci-app-pbr luci-i18n-pbr-ru amnezia-xray-core
  SUPPORTED_DEVICES := xiaomi,be5000
  ARTIFACTS :=
endef
TARGET_DEVICES += xiaomi_be5000-full
