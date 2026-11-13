include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=qca-nss-qinq
PKG_RELEASE:=3
PKG_LICENSE:=GPL-2.0-only

include $(INCLUDE_DIR)/package.mk

define KernelPackage/qca-nss-qinq
  SECTION:=kernel
  CATEGORY:=Kernel modules
  SUBMENU:=Network Devices
  TITLE:=Native NSS QinQ nodes for YT921x DSA ports
  DEPENDS:=@TARGET_qualcommax_ipq50xx +kmod-qca-nss-drv +@NSS_DRV_VLAN_ENABLE
  FILES:=$(PKG_BUILD_DIR)/qca-nss-qinq.ko
  AUTOLOAD:=$(call AutoLoad,61,qca-nss-qinq)
endef

define KernelPackage/qca-nss-qinq/description
  Native qosmio NSS VLAN node chains for YT921x QinQ DSA transport.
  One root per physical port SVID and one leaf per inner VID; no stmmac
  takeover API or manual arm step. Ports stay under the YT921x DSA driver.
endef

define KernelPackage/qca-nss-qinq/install
	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_BIN) ./files/99-qca-nss-qinq-firewall $(1)/etc/uci-defaults/
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	+$(MAKE) -C "$(LINUX_DIR)" \
	  CROSS_COMPILE="$(TARGET_CROSS)" \
	  ARCH="$(LINUX_KARCH)" \
	  M="$(PKG_BUILD_DIR)" \
	  NOSTDINC_FLAGS="$(KERNEL_NOSTDINC_FLAGS) -I$(STAGING_DIR)/usr/include/qca-nss-drv" \
	  $(KERNEL_MAKE_FLAGS) \
	  modules
endef

$(eval $(call KernelPackage,qca-nss-qinq))
