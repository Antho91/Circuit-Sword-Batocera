// SPDX-License-Identifier: GPL-2.0
/*
 * circuitsword_battery — minimal virtual battery for the Circuit Sword,
 * ported from Retropie_source/battery-driver/cs_battery.c.
 *
 * Registers /sys/class/power_supply/circuitsword-battery so
 * EmulationStation's native BatteryIconComponent (es-core/src/utils/Platform.cpp,
 * which scans /sys/class/power_supply directly) works with zero ES-side
 * changes. power_supply is a kernel-only class; userspace can't create one
 * directly, so this tiny module exists purely to expose three values that
 * userspace (the rpi-circuitsword.py daemon) writes into it every poll:
 *
 *     echo 47 > /sys/module/circuitsword_battery/parameters/capacity
 *     echo 1  > /sys/module/circuitsword_battery/parameters/charging
 *     echo 1  > /sys/module/circuitsword_battery/parameters/online
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/minmax.h>
#include <linux/power_supply.h>

static int capacity = 50;          /* 0..100, written by rpi-circuitsword.py */
static int charging;               /* 0 = discharging, 1 = charging */
static int online;                 /* 0 = no external power, 1 = external power present */
module_param(capacity, int, 0644);
MODULE_PARM_DESC(capacity, "Battery charge level 0-100 (written by rpi-circuitsword.py)");
module_param(charging, int, 0644);
MODULE_PARM_DESC(charging, "1 = charging, 0 = discharging (written by rpi-circuitsword.py)");
module_param(online, int, 0644);
MODULE_PARM_DESC(online, "1 = external/USB power present, 0 = not present (written by rpi-circuitsword.py)");

static enum power_supply_property circuitsword_battery_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_ONLINE,
};

static int circuitsword_battery_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = charging ? POWER_SUPPLY_STATUS_CHARGING
				       : POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = clamp(capacity, 0, 100);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LIPO;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_SYSTEM;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = online;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct power_supply_desc circuitsword_battery_desc = {
	// EmulationStation's battery detection (es-core/src/utils/Platform.cpp)
	// scans /sys/class/power_supply for an entry whose name starts with
	// "bat" (case-insensitive) -- "circuitsword-battery" doesn't match
	// (no "/bat" path segment), so ES never found it. Confirmed on real
	// hardware: sysfs/module params worked, but no battery icon in ES.
	.name		= "battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= circuitsword_battery_props,
	.num_properties	= ARRAY_SIZE(circuitsword_battery_props),
	.get_property	= circuitsword_battery_get_property,
};

static struct power_supply *circuitsword_battery_psy;

static int __init circuitsword_battery_init(void)
{
	struct power_supply_config cfg = {};

	circuitsword_battery_psy = power_supply_register(NULL, &circuitsword_battery_desc, &cfg);
	if (IS_ERR(circuitsword_battery_psy))
		return PTR_ERR(circuitsword_battery_psy);

	pr_info("circuitsword_battery: registered virtual battery for EmulationStation\n");
	return 0;
}

static void __exit circuitsword_battery_exit(void)
{
	power_supply_unregister(circuitsword_battery_psy);
}

module_init(circuitsword_battery_init);
module_exit(circuitsword_battery_exit);

MODULE_AUTHOR("Circuit Sword");
MODULE_DESCRIPTION("Virtual battery feeding EmulationStation's battery icon");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1.0");
