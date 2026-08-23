################################################################################
#
# circuitsword-rtk-bluetooth
#
################################################################################

# Vendored, not built from source. rtk_hciattach is Realtek's own H5
# attach tool -- unlike bluez's generic `btattach -P 3wire` (which only
# does the line-discipline switch and H5 sync, then fails with "Protocol
# driver not attached"), rtk_hciattach also handles the Realtek-specific
# firmware/config download over the same link, which this chip needs
# before the HCI device actually comes up. rtlbt_fw/rtlbt_config are the
# matching firmware blobs -- both binary and firmware carried over as-is
# from the original RetroPie-based Circuit-Sword build
# (https://github.com/Antho91/Circuit-Sword, bt-driver/), the only
# proven-working combination for this exact chip/board -- confirmed
# compatible with this image's glibc 2.40 (binary requires up to 2.34).
CIRCUITSWORD_RTK_BLUETOOTH_VERSION = 1.0
CIRCUITSWORD_RTK_BLUETOOTH_SOURCE =
CIRCUITSWORD_RTK_BLUETOOTH_LICENSE = PROPRIETARY

CIRCUITSWORD_RTK_BLUETOOTH_SRC = $(BR2_EXTERNAL_BATOCERA_PATH)/package/batocera/utils/circuitsword-rtk-bluetooth

define CIRCUITSWORD_RTK_BLUETOOTH_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(CIRCUITSWORD_RTK_BLUETOOTH_SRC)/rtk_hciattach \
	    $(TARGET_DIR)/usr/bin/rtk_hciattach
	$(INSTALL) -D -m 0644 $(CIRCUITSWORD_RTK_BLUETOOTH_SRC)/rtlbt_fw \
	    $(TARGET_DIR)/lib/firmware/rtl_bt/rtlbt_fw
	$(INSTALL) -D -m 0644 $(CIRCUITSWORD_RTK_BLUETOOTH_SRC)/rtlbt_config \
	    $(TARGET_DIR)/lib/firmware/rtl_bt/rtlbt_config
endef

$(eval $(generic-package))
