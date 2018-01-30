/*
** =============================================================================
** Copyright (c) 2017  Nikolay Karev
** Modified for AW8738 by Tamas Kemenesi
** This program is free software; you can redistribute it and/or modify it under
** the terms of the GNU General Public License as published by the Free Software
** Foundation; version 2.
**
** This program is distributed in the hope that it will be useful, but WITHOUT
** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
** FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License along with
** this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
** Street, Fifth Floor, Boston, MA 02110-1301, USA.
**
** File:
**     aw8738.c
**
** Description:
**     misc driver for Texas Instruments AW8738 High Performance 4W Smart Amplifier
**
** =============================================================================
*/


#define DEBUG
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <asm/uaccess.h>
#include <linux/i2c-dev.h>



#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>


#define DRV_NAME "aw8738"

#define AW8738_MODE 6

#define EXT_CLASS_D_EN_DELAY 13000
#define EXT_CLASS_D_DIS_DELAY 3000
#define EXT_CLASS_D_DELAY_DELTA 2000

static int spk_pa_gpio = -1;
static int value = 0;

static void amplifier_enable(void) {
  int i = 0;
  pr_info("Enabling amplifier in kernel\n");
	/* Open external audio PA device */
	for (i = 0; i < AW8738_MODE; i++) {
		gpio_direction_output(spk_pa_gpio, false);
		gpio_direction_output(spk_pa_gpio, true);
	}
	usleep_range(EXT_CLASS_D_EN_DELAY,
	EXT_CLASS_D_EN_DELAY + EXT_CLASS_D_DELAY_DELTA);
}

static void amplifier_disable(void) {
  gpio_direction_output(spk_pa_gpio, false);
  pr_info("Disabling amplifier in kernel\n");
  usleep_range(EXT_CLASS_D_DIS_DELAY,
   EXT_CLASS_D_DIS_DELAY + EXT_CLASS_D_DELAY_DELTA);
}

static int init_gpio(struct platform_device *pdev) {
  spk_pa_gpio = of_get_named_gpio(pdev->dev.of_node, "ext-spk-amp-gpio", 0);
	if (spk_pa_gpio < 0) {
		dev_err(&pdev->dev,
		"%s: error! spk_pa_gpio is :%d\n", __func__, spk_pa_gpio);
	} else {
		if (gpio_request_one(spk_pa_gpio, GPIOF_DIR_OUT, "spk_enable")) {
			pr_err("%s: request spk_pa_gpio  fail!\n", __func__);
		}
	}
	pr_err("%s: [hjf] request spk_pa_gpio is %d!\n", __func__, spk_pa_gpio);
  gpio_direction_output(spk_pa_gpio, 0);
  return 0;
}

static ssize_t enable_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", value);
}

static ssize_t enable_store(struct kobject *kobj,
			struct kobj_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%du", &value);
  if (value) {
    amplifier_enable();
  } else {
    amplifier_disable();
  }
	return count;
}

static struct kobj_attribute enable_attr =
	__ATTR(enable, 0664, enable_show, enable_store);

static struct kobject *kobj;

static int aw8738_machine_probe(struct platform_device *pdev)
{
	int ret = init_gpio(pdev);
  if (ret) {
    pr_err("Error initializing amplifier gpio: %d\n", ret);
  }

	/* create a dir in sys/ */
	kobj = kobject_create_and_add("audio_amplifier", NULL);
	if (!kobj)
		return - ENOMEM;

	/* create a attribute file in kobj_example */
 	ret = sysfs_create_file(kobj, &enable_attr.attr);
	if (ret)
		goto attr_file_failed;
	return 0;
attr_file_failed:
	kobject_put(kobj);
	return ret;
}

static int aw8738_machine_remove(struct platform_device *pdev) {
  return 0;
}

static const struct of_device_id aw8738_machine_of_match[]  = {
	{ .compatible = "aw,aw8738", },
	{},
};
static int snd_soc_pm(struct device *dev) {
  return 0;
};

const struct dev_pm_ops pm_ops = {
	.suspend = &snd_soc_pm,
	.resume = &snd_soc_pm,
	.freeze = &snd_soc_pm,
	.thaw = &snd_soc_pm,
	.poweroff = &snd_soc_pm,
	.restore = &snd_soc_pm,
};


static struct platform_driver aw8738_machine_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.pm = &pm_ops,
		.of_match_table = aw8738_machine_of_match,
	},
	.probe = aw8738_machine_probe,
	.remove = aw8738_machine_remove,
};

static int __init aw8738_machine_init(void)
{
	return platform_driver_register(&aw8738_machine_driver);
}

late_initcall(aw8738_machine_init);

static void __exit aw8738_machine_exit(void)
{
	return platform_driver_unregister(&aw8738_machine_driver);
}

module_exit(aw8738_machine_exit);

MODULE_DESCRIPTION("aw8738 amplifier");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, aw8738_machine_of_match);
