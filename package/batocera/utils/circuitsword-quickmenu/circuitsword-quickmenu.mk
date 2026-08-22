################################################################################
#
# circuitsword-quickmenu
#
################################################################################

CIRCUITSWORD_QUICKMENU_VERSION = 2.0
CIRCUITSWORD_QUICKMENU_SOURCE =
CIRCUITSWORD_QUICKMENU_LICENSE = GPL-2.0+
CIRCUITSWORD_QUICKMENU_DEPENDENCIES = host-wayland wayland wayland-protocols freetype

# NOTE: deliberately not named *_SRCDIR -- that name is reserved by
# Buildroot's own package infra (package/pkg-generic.mk unconditionally
# sets <PKG>_SRCDIR = <PKG>_DIR/<PKG>_SUBDIR after this file is included,
# which silently clobbers a same-named variable defined here and points
# wayland-scanner at the empty build dir instead of our package sources).
CIRCUITSWORD_QUICKMENU_PKGDIR = $(BR2_EXTERNAL_BATOCERA_PATH)/package/batocera/utils/circuitsword-quickmenu

CIRCUITSWORD_QUICKMENU_SRCS = \
	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/quickmenu.c \
	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_font.c \
	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_icon_data.c \
	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_icons.c \
	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_input.c \
	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_joystick.c \
	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_settings.c \
	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_ttf.c \
	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_wl.c

# wlr-layer-shell-unstable-v1 is NOT shipped by the wayland-protocols
# package -- it comes from wlroots/labwc. It is vendored here, copied
# verbatim (licence header included) from labwc's own source tree at
# protocols/wlr-layer-shell-unstable-v1.xml, exactly as labwc's
# clients/meson.build references it.
CIRCUITSWORD_QUICKMENU_LAYER_SHELL_XML = $(CIRCUITSWORD_QUICKMENU_PKGDIR)/protocols/wlr-layer-shell-unstable-v1.xml
# xdg-shell DOES come from wayland-protocols, installed into staging.
CIRCUITSWORD_QUICKMENU_XDG_SHELL_XML = $(STAGING_DIR)/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml

define CIRCUITSWORD_QUICKMENU_BUILD_CMDS
	$(HOST_DIR)/bin/wayland-scanner client-header \
		$(CIRCUITSWORD_QUICKMENU_LAYER_SHELL_XML) \
		$(@D)/wlr-layer-shell-unstable-v1-client-protocol.h
	$(HOST_DIR)/bin/wayland-scanner private-code \
		$(CIRCUITSWORD_QUICKMENU_LAYER_SHELL_XML) \
		$(@D)/wlr-layer-shell-unstable-v1-protocol.c
	$(HOST_DIR)/bin/wayland-scanner client-header \
		$(CIRCUITSWORD_QUICKMENU_XDG_SHELL_XML) \
		$(@D)/xdg-shell-client-protocol.h
	$(HOST_DIR)/bin/wayland-scanner private-code \
		$(CIRCUITSWORD_QUICKMENU_XDG_SHELL_XML) \
		$(@D)/xdg-shell-protocol.c
	$(TARGET_CONFIGURE_OPTS) $(TARGET_CC) \
		-std=gnu99 -O2 -Wall -Wextra \
		-I$(CIRCUITSWORD_QUICKMENU_PKGDIR) \
		-I$(@D) \
		-I$(STAGING_DIR)/usr/include \
		-I$(STAGING_DIR)/usr/include/freetype2 \
		$(CIRCUITSWORD_QUICKMENU_SRCS) \
		$(@D)/wlr-layer-shell-unstable-v1-protocol.c \
		$(@D)/xdg-shell-protocol.c \
		-o $(@D)/circuitsword-quickmenu \
		-L$(STAGING_DIR)/usr/lib -lwayland-client -lrt -lfreetype
	$(TARGET_CONFIGURE_OPTS) $(TARGET_CC) \
		-std=gnu99 -O2 -Wall -Wextra \
		-I$(CIRCUITSWORD_QUICKMENU_PKGDIR) \
		-I$(@D) \
		-I$(STAGING_DIR)/usr/include \
		$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_wl_selftest.c \
		$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_wl.c \
		$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_font.c \
		$(@D)/wlr-layer-shell-unstable-v1-protocol.c \
		$(@D)/xdg-shell-protocol.c \
		-o $(@D)/qm-wl-selftest \
		-L$(STAGING_DIR)/usr/lib -lwayland-client -lrt
endef

define CIRCUITSWORD_QUICKMENU_INSTALL_TARGET_CMDS
	$(INSTALL) -m 0755 -D $(@D)/circuitsword-quickmenu \
		$(TARGET_DIR)/usr/bin/circuitsword-quickmenu
	$(INSTALL) -m 0644 -D $(CIRCUITSWORD_QUICKMENU_PKGDIR)/fonts/Cabin-Regular.ttf \
		$(TARGET_DIR)/usr/share/circuitsword-quickmenu/Cabin-Regular.ttf
endef

$(eval $(generic-package))
