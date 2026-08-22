################################################################################
#
# circuitsword-statusbar
#
################################################################################

CIRCUITSWORD_STATUSBAR_VERSION = 1.0
CIRCUITSWORD_STATUSBAR_SOURCE =
CIRCUITSWORD_STATUSBAR_LICENSE = GPL-2.0+
CIRCUITSWORD_STATUSBAR_DEPENDENCIES = host-wayland wayland wayland-protocols

# See circuitsword-quickmenu.mk's own NOTE: deliberately not named
# *_SRCDIR, which Buildroot's package infra reserves for itself.
CIRCUITSWORD_STATUSBAR_PKGDIR = $(BR2_EXTERNAL_BATOCERA_PATH)/package/batocera/utils/circuitsword-statusbar
# qm_font.c, qm_icon_data.c, qm_icons.c, qm_settings.c and quickmenu.h are
# owned by circuitsword-quickmenu and reused here as-is (shared pure
# primitives, no toolkit, no shared library) -- see
# docs/superpowers/specs/2026-08-10-persistent-statusbar-design.md.
# Deliberately scoped to this package (not CIRCUITSWORD_QUICKMENU_PKGDIR,
# which circuitsword-quickmenu.mk defines for its own use) -- Buildroot
# evaluates all package .mk files in one shared Make namespace, so two
# packages defining the same global variable name is a latent hazard. Also
# deliberately not named *_SRCDIR, which Buildroot's package infra reserves
# for itself.
CIRCUITSWORD_STATUSBAR_QUICKMENU_SRCDIR = $(BR2_EXTERNAL_BATOCERA_PATH)/package/batocera/utils/circuitsword-quickmenu

CIRCUITSWORD_STATUSBAR_SRCS = \
	$(CIRCUITSWORD_STATUSBAR_PKGDIR)/statusbar.c \
	$(CIRCUITSWORD_STATUSBAR_PKGDIR)/sb_render.c \
	$(CIRCUITSWORD_STATUSBAR_PKGDIR)/sb_wl.c \
	$(CIRCUITSWORD_STATUSBAR_QUICKMENU_SRCDIR)/qm_font.c \
	$(CIRCUITSWORD_STATUSBAR_QUICKMENU_SRCDIR)/qm_icon_data.c \
	$(CIRCUITSWORD_STATUSBAR_QUICKMENU_SRCDIR)/qm_icons.c \
	$(CIRCUITSWORD_STATUSBAR_QUICKMENU_SRCDIR)/qm_settings.c

# Same vendored/staging protocol XML sources circuitsword-quickmenu uses --
# the layer-shell XML is vendored once, in circuitsword-quickmenu's own
# package directory, and referenced from here rather than duplicated.
CIRCUITSWORD_STATUSBAR_LAYER_SHELL_XML = $(CIRCUITSWORD_STATUSBAR_QUICKMENU_SRCDIR)/protocols/wlr-layer-shell-unstable-v1.xml
CIRCUITSWORD_STATUSBAR_XDG_SHELL_XML = $(STAGING_DIR)/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml

define CIRCUITSWORD_STATUSBAR_BUILD_CMDS
	$(HOST_DIR)/bin/wayland-scanner client-header \
		$(CIRCUITSWORD_STATUSBAR_LAYER_SHELL_XML) \
		$(@D)/wlr-layer-shell-unstable-v1-client-protocol.h
	$(HOST_DIR)/bin/wayland-scanner private-code \
		$(CIRCUITSWORD_STATUSBAR_LAYER_SHELL_XML) \
		$(@D)/wlr-layer-shell-unstable-v1-protocol.c
	$(HOST_DIR)/bin/wayland-scanner client-header \
		$(CIRCUITSWORD_STATUSBAR_XDG_SHELL_XML) \
		$(@D)/xdg-shell-client-protocol.h
	$(HOST_DIR)/bin/wayland-scanner private-code \
		$(CIRCUITSWORD_STATUSBAR_XDG_SHELL_XML) \
		$(@D)/xdg-shell-protocol.c
	$(TARGET_CONFIGURE_OPTS) $(TARGET_CC) \
		-std=gnu99 -O2 -Wall -Wextra \
		-I$(CIRCUITSWORD_STATUSBAR_PKGDIR) \
		-I$(CIRCUITSWORD_STATUSBAR_QUICKMENU_SRCDIR) \
		-I$(@D) \
		-I$(STAGING_DIR)/usr/include \
		$(CIRCUITSWORD_STATUSBAR_SRCS) \
		$(@D)/wlr-layer-shell-unstable-v1-protocol.c \
		$(@D)/xdg-shell-protocol.c \
		-o $(@D)/circuitsword-statusbar \
		-L$(STAGING_DIR)/usr/lib -lwayland-client -lrt
endef

define CIRCUITSWORD_STATUSBAR_INSTALL_TARGET_CMDS
	$(INSTALL) -m 0755 -D $(@D)/circuitsword-statusbar \
		$(TARGET_DIR)/usr/bin/circuitsword-statusbar
endef

$(eval $(generic-package))
