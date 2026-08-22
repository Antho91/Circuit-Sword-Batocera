// SPDX-License-Identifier: GPL-2.0
/*
 * circuitsword_backlight — virtual backlight class device for the Circuit
 * Sword. Registers /sys/class/backlight/circuitsword-backlight so
 * Batocera's own batocera-brightness script and EmulationStation's
 * brightness UI (both of which scan /sys/class/backlight directly) work
 * with zero changes on their side.
 *
 * The actual backlight hardware is driven by the on-board Arduino Leonardo
 * over a serial link (CMD_SET_BL), which the kernel has no direct access
 * to. This module only tracks the current value; the rpi-circuitsword.py
 * daemon polls .brightness for changes and forwards them to the Arduino
 * (a sysfs write can't itself notify userspace synchronously, so a
 * userspace poll loop is required either way).
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/backlight.h>

#define CIRCUITSWORD_BACKLIGHT_MAX 100

static int circuitsword_backlight_update_status(struct backlight_device *bd)
{
	/* Nothing to do here: the daemon reads .brightness itself via sysfs
	 * and forwards it to the Arduino on its own poll cycle. This
	 * callback exists only because backlight_ops requires one. */
	return 0;
}

static int circuitsword_backlight_get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

static const struct backlight_ops circuitsword_backlight_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= circuitsword_backlight_update_status,
	.get_brightness	= circuitsword_backlight_get_brightness,
};

static struct backlight_device *circuitsword_backlight_dev;

static int __init circuitsword_backlight_init(void)
{
	struct backlight_properties props;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = CIRCUITSWORD_BACKLIGHT_MAX;
	props.brightness = CIRCUITSWORD_BACKLIGHT_MAX;

	circuitsword_backlight_dev = backlight_device_register(
		"circuitsword-backlight", NULL, NULL,
		&circuitsword_backlight_ops, &props);
	if (IS_ERR(circuitsword_backlight_dev))
		return PTR_ERR(circuitsword_backlight_dev);

	pr_info("circuitsword_backlight: registered virtual backlight for batocera-brightness/ES\n");
	return 0;
}

static void __exit circuitsword_backlight_exit(void)
{
	backlight_device_unregister(circuitsword_backlight_dev);
}

module_init(circuitsword_backlight_init);
module_exit(circuitsword_backlight_exit);

MODULE_AUTHOR("Circuit Sword");
MODULE_DESCRIPTION("Virtual backlight class device bridging batocera-brightness/ES to the Arduino");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1.0");
