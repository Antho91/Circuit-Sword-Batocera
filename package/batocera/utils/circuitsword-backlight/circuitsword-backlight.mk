################################################################################
#
# circuitsword-backlight
#
################################################################################

CIRCUITSWORD_BACKLIGHT_VERSION = 1.0
CIRCUITSWORD_BACKLIGHT_SITE = $(BR2_EXTERNAL_BATOCERA_PATH)/package/batocera/utils/circuitsword-backlight
CIRCUITSWORD_BACKLIGHT_SITE_METHOD = local

$(eval $(kernel-module))
$(eval $(generic-package))
