include $(TOPDIR)/rules.mk

PKG_NAME:=nss-wan-qos
PKG_RELEASE:=2
PKG_LICENSE:=GPL-2.0-only
PKG_FLAGS:=nonshared

include $(INCLUDE_DIR)/package.mk

define Package/nss-wan-qos
  SECTION:=net
  CATEGORY:=Network
  TITLE:=NSS WAN rate limits with flow queuing
  DEPENDS:=@TARGET_qualcommax_ipq50xx +kmod-qca-nss-drv-igs +tc +ip-full +flock
endef

define Package/nss-wan-qos/description
Configure NSS token-bucket shapers and FQ-CoDel for a routed WAN
port with a dedicated QinQ DSA conduit.
endef

define Build/Compile
endef

define Package/nss-wan-qos/conffiles
/etc/config/nss_wan_qos
endef

define Package/nss-wan-qos/install
	$(INSTALL_DIR) $(1)/etc/config $(1)/etc/init.d $(1)/usr/libexec
	$(INSTALL_CONF) ./files/nss_wan_qos.config $(1)/etc/config/nss_wan_qos
	$(INSTALL_BIN) ./files/nss-wan-qos.init $(1)/etc/init.d/nss-wan-qos
	$(INSTALL_BIN) ./files/nss-wan-qos.sh $(1)/usr/libexec/nss-wan-qos
endef

$(eval $(call BuildPackage,nss-wan-qos))
