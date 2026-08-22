################################################################################
#
# m4
#
################################################################################

M4_VERSION = 1.4.19
M4_SOURCE = m4-$(M4_VERSION).tar.xz
M4_SITE = $(BR2_GNU_MIRROR)/m4
M4_LICENSE = GPL-3.0+
M4_LICENSE_FILES = COPYING

# gcc-15 defaults to -std=gnu23 which is incorrectly detected and
# generates build failures in the gnulib copy included in m4-1.4.19.
# Force the previous gcc default standard (-std=gnu17) unconditionally —
# this pinned Buildroot checkout predates BR2_HOST_GCC_AT_LEAST_15
# detection (added upstream in commit 7a07a9d155b8f601d68f07ee0ed1dc8d48907644),
# and -std=gnu17 is harmless on any host gcc that supports it (8+).
HOST_M4_CONF_ENV = CFLAGS="$(HOST_CFLAGS) -std=gnu17"

$(eval $(host-autotools-package))
