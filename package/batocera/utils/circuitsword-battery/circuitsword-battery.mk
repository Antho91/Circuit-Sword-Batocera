################################################################################
#
# circuitsword-battery
#
################################################################################

CIRCUITSWORD_BATTERY_VERSION = 1.0
CIRCUITSWORD_BATTERY_SITE = $(BR2_EXTERNAL_BATOCERA_PATH)/package/batocera/utils/circuitsword-battery
CIRCUITSWORD_BATTERY_SITE_METHOD = local

$(eval $(kernel-module))
$(eval $(generic-package))
