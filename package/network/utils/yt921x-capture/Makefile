include $(TOPDIR)/rules.mk

PKG_NAME:=yt921x-capture
PKG_VERSION:=1.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-or-later
PKG_FILE_DEPENDS:=$(TOPDIR)/tools/nss/yt921x_capture.c

include $(INCLUDE_DIR)/package.mk

define Package/yt921x-capture
  SECTION:=net
  CATEGORY:=Network
  TITLE:=YT921x capture normalization and port metadata
  DEPENDS:=+libpcap
endef

define Package/yt921x-capture/description
  Bounded packet capture with paired wire and Ethernet views, ordinary
  userspace BPF filters, VLAN metadata reconstruction and private port metadata.
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) $(TOPDIR)/tools/nss/yt921x_capture.c $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -std=c11 \
		-Wall -Wextra -Werror $(PKG_BUILD_DIR)/yt921x_capture.c \
		$(TARGET_LDFLAGS) -lpcap -o $(PKG_BUILD_DIR)/yt921x-capture
endef

define Package/yt921x-capture/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/yt921x-capture $(1)/usr/bin/
endef

$(eval $(call BuildPackage,yt921x-capture))
