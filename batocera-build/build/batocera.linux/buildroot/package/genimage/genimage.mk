################################################################################
#
# genimage
#
################################################################################
# batocera - bump
GENIMAGE_VERSION = 19
GENIMAGE_SOURCE = genimage-$(GENIMAGE_VERSION).tar.xz
GENIMAGE_SITE = https://github.com/pengutronix/genimage/releases/download/v$(GENIMAGE_VERSION)
# Circuit-Sword: genimage shells out to mkdosfs at image-generation time
# for vfat boot partitions (our board's genimage.cfg needs boot.vfat) --
# nothing else in this build pulled in host-dosfstools, so mkdosfs was
# missing entirely, causing "mkdosfs: not found" during target-post-image.
# It also shells out to mcopy (mtools) to populate the formatted vfat
# image with files -- same gap, "mcopy: not found" was the next failure
# once mkdosfs was fixed.
HOST_GENIMAGE_DEPENDENCIES = host-pkgconf host-libconfuse host-dosfstools host-mtools
GENIMAGE_LICENSE = GPL-2.0
GENIMAGE_LICENSE_FILES = COPYING

$(eval $(host-autotools-package))
