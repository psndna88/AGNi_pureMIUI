/*
 * FPC1020 Fingerprint sensor device driver
 *
 * This driver will control the platform resources that the FPC fingerprint
 * sensor needs to operate. The major things are probing the sensor to check
 * that it is actually connected and let the Kernel know this and with that also
 * enabling and disabling of regulators, enabling and disabling of platform
 * clocks, controlling GPIOs such as SPI chip select, sensor reset line, sensor
 * IRQ line, MISO and MOSI lines.
 *
 * The driver will expose most of its available functionality in sysfs which
 * enables dynamic control of these features from eg. a user space process.
 *
 * The sensor's IRQ events will be pushed to Kernel's event handling system and
 * are exposed in the drivers event node. This makes it possible for a user
 * space process to poll the input node and receive IRQ events easily. Usually
 * this node is available under /dev/input/eventX where 'X' is a number given by
 * the event system. A user space process will need to traverse all the event
 * nodes and ask for its parent's name (through EVIOCGNAME) which should match
 * the value in device tree named input-device-name.
 *
 * This driver will NOT send any SPI commands to the sensor it only controls the
 * electrical parts.
 *
 *
 * Copyright (c) 2015 Fingerprint Cards AB <tech@fingerprints.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License Version 2
 * as published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <soc/qcom/scm.h>
#include <linux/platform_device.h>
#include <linux/wakelock.h>
#include <linux/input.h>
#include <linux/display_state.h>

#define KEY_FINGERPRINT 0x2ee

#ifdef CONFIG_MSM_HOTPLUG
#include <linux/msm_hotplug.h>
#include <linux/workqueue.h>
#include <linux/init.h>
#endif

#define FPC1020_RESET_LOW_US 1000
#define FPC1020_RESET_HIGH1_US 100
#define FPC1020_RESET_HIGH2_US 1250
#define FPC_TTW_HOLD_TIME 1000

#ifdef CONFIG_MSM_HOTPLUG
extern void msm_hotplug_resume_timeout(void);
#endif

struct vreg_config {
	char *name;
	unsigned long vmin;
	unsigned long vmax;
	int ua_load;
};

static const struct vreg_config vreg_conf[] = {
	{"vcc_spi", 1800000UL, 1800000UL, 20000,},
};

struct fpc1020_data {
	struct device *dev;
	struct regulator *vreg[ARRAY_SIZE(vreg_conf)];
	struct wake_lock ttw_wl;
	int irq_gpio;
	int rst_gpio;
	struct mutex lock;
	bool wakeup_enabled;
	struct input_dev *input_dev;
};

unsigned int kenzo_fpsensor = 1;
static int __init setup_kenzo_fpsensor(char *str)
{
	if (!strncmp(str, "gdx", strlen(str)))
		kenzo_fpsensor = 2;

	return kenzo_fpsensor;
}
__setup("androidboot.fpsensor=", setup_kenzo_fpsensor);

static int vreg_setup(struct fpc1020_data *fpc1020, const char *name,
		      bool enable)
{
	size_t i;
	int rc;
	struct regulator *vreg;
	struct device *dev = fpc1020->dev;

	for (i = 0; i < ARRAY_SIZE(fpc1020->vreg); i++) {
		const char *n = vreg_conf[i].name;
		if (!strncmp(n, name, strlen(n)))
			goto found;
	}
	dev_err(dev, "Regulator %s not found\n", name);
	return -EINVAL;
found:
	vreg = fpc1020->vreg[i];
	if (enable) {
		if (!vreg) {
			vreg = regulator_get(dev, name);
			if (!vreg) {
				dev_err(dev, "Unable to get  %s\n", name);
				return -ENODEV;
			}
		}
		if (regulator_count_voltages(vreg) > 0) {
			rc = regulator_set_voltage(vreg, vreg_conf[i].vmin,
						   vreg_conf[i].vmax);
			if (rc)
				dev_err(dev,
					"Unable to set voltage on %s, %d\n",
					name, rc);
		}
		rc = regulator_set_optimum_mode(vreg, vreg_conf[i].ua_load);
		if (rc < 0)
			dev_err(dev, "Unable to set current on %s, %d\n",
				name, rc);
		rc = regulator_enable(vreg);
		if (rc) {
			dev_err(dev, "error enabling %s: %d\n", name, rc);
			regulator_put(vreg);
			vreg = NULL;
		}
		fpc1020->vreg[i] = vreg;
	} else {
		if (vreg) {
			if (regulator_is_enabled(vreg)) {
				regulator_disable(vreg);
				dev_dbg(dev, "disabled %s\n", name);
			}
			regulator_put(vreg);
			fpc1020->vreg[i] = NULL;
		}
		rc = 0;
	}
	return rc;
}

static int hw_reset(struct fpc1020_data *fpc1020)
{
	int irq_gpio;
	struct device *dev = fpc1020->dev;

	gpio_set_value(fpc1020->rst_gpio, 1);
	udelay(FPC1020_RESET_HIGH1_US);

	gpio_set_value(fpc1020->rst_gpio, 0);
	udelay(FPC1020_RESET_LOW_US);

	gpio_set_value(fpc1020->rst_gpio, 1);
	udelay(FPC1020_RESET_HIGH2_US);

	irq_gpio = gpio_get_value(fpc1020->irq_gpio);

	dev_info(dev, "IRQ after reset %d\n", irq_gpio);
	return 0;
}

static ssize_t hw_reset_set(struct device *dev,
			    struct device_attribute *attr, const char *buf,
			    size_t count)
{
	int rc;
	struct fpc1020_data *fpc1020 = dev_get_drvdata(dev);

	if (!strncmp(buf, "reset", strlen("reset")))
		rc = hw_reset(fpc1020);
	else
		return -EINVAL;
	return rc ? rc : count;
}

static DEVICE_ATTR(hw_reset, S_IWUSR | S_IWGRP | S_IWOTH, NULL, hw_reset_set);

/**
 * sysfs node for controlling whether the driver is allowed
 * to wake up the platform on interrupt.
 */
static ssize_t wakeup_enable_set(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct fpc1020_data *fpc1020 = dev_get_drvdata(dev);

	if (!strncmp(buf, "enable", strlen("enable"))) {
		fpc1020->wakeup_enabled = true;
		smp_wmb();
	} else if (!strncmp(buf, "disable", strlen("disable"))) {
		fpc1020->wakeup_enabled = false;
		smp_wmb();
	} else
		return -EINVAL;

	return count;
}

static DEVICE_ATTR(wakeup_enable, S_IWUSR | S_IWGRP | S_IWOTH, NULL, wakeup_enable_set);

/**
 * sysf node to check the interrupt status of the sensor, the interrupt
 * handler should perform sysf_notify to allow userland to poll the node.
 */
static ssize_t irq_get(struct device* device,
			     struct device_attribute* attribute,
			     char* buffer)
{
	struct fpc1020_data* fpc1020 = dev_get_drvdata(device);
	int irq = gpio_get_value(fpc1020->irq_gpio);
	return scnprintf(buffer, PAGE_SIZE, "%i\n", irq);
}


/**
 * writing to the irq node will just drop a printk message
 * and return success, used for latency measurement.
 */
static ssize_t irq_ack(struct device* device,
			     struct device_attribute* attribute,
			     const char* buffer, size_t count)
{
	struct fpc1020_data* fpc1020 = dev_get_drvdata(device);
	dev_dbg(fpc1020->dev, "%s\n", __func__);
	return count;
}

static DEVICE_ATTR(irq, S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IWOTH, irq_get, irq_ack);

static struct attribute *attributes[] = {
	&dev_attr_hw_reset.attr,
	&dev_attr_wakeup_enable.attr,
	&dev_attr_irq.attr,
	NULL
};

static const struct attribute_group attribute_group = {
	.attrs = attributes,
};

#ifdef CONFIG_MSM_HOTPLUG
static void __cpuinit msm_hotplug_resume_call(struct work_struct *msm_hotplug_resume_call_work)
{
	msm_hotplug_resume_timeout();
}
static __refdata DECLARE_WORK(msm_hotplug_resume_call_work, msm_hotplug_resume_call);
#endif

static int fpc1020_input_init(struct fpc1020_data * fpc1020)
{
	int ret;

	fpc1020->input_dev = input_allocate_device();
	if (!fpc1020->input_dev) {
		pr_err("fingerprint input boost allocation is fucked - 1 star\n");
		ret = -ENOMEM;
		goto exit;
	}

	fpc1020->input_dev->name = "fpc1020";
	fpc1020->input_dev->evbit[0] = BIT(EV_KEY);

	set_bit(KEY_FINGERPRINT, fpc1020->input_dev->keybit);

	ret = input_register_device(fpc1020->input_dev);
	if (ret) {
		pr_err("fingerprint boost input registration is fucked - fixpls\n");
		goto err_free_dev;
	}

	return 0;

err_free_dev:
	input_free_device(fpc1020->input_dev);
exit:
	return ret;
}

static irqreturn_t fpc1020_irq_handler(int irq, void *handle)
{
	struct fpc1020_data *fpc1020 = handle;
	dev_dbg(fpc1020->dev, "%s\n", __func__);

	/* Make sure 'wakeup_enabled' is updated before using it
	 ** since this is interrupt context (other thread...) */
	smp_rmb();

	if (fpc1020->wakeup_enabled) {
		wake_lock_timeout(&fpc1020->ttw_wl, msecs_to_jiffies(FPC_TTW_HOLD_TIME));
#ifdef CONFIG_MSM_HOTPLUG
		if (msm_enabled && msm_hotplug_scr_suspended &&
		   !msm_hotplug_fingerprint_called) {
			msm_hotplug_fingerprint_called = true;
			schedule_work(&msm_hotplug_resume_call_work);
		}
#endif
	}

	sysfs_notify(&fpc1020->dev->kobj, NULL, dev_attr_irq.attr.name);

/* On touch the fp sensor, boost the cpu even if screen is on */
#ifdef CONFIG_MSM_HOTPLUG
	if (fp_bigcore_boost) {
#endif
		sched_set_boost(1);
#ifdef CONFIG_MSM_HOTPLUG
	}
#endif
	if (!is_display_on()) {
		sched_set_boost(1);
		input_report_key(fpc1020->input_dev, KEY_FINGERPRINT, 1);
		input_sync(fpc1020->input_dev);
		input_report_key(fpc1020->input_dev, KEY_FINGERPRINT, 0);
		input_sync(fpc1020->input_dev);
		sched_set_boost(0);
	}
#ifdef CONFIG_MSM_HOTPLUG
	if (fp_bigcore_boost) {
#endif
		sched_set_boost(0);
#ifdef CONFIG_MSM_HOTPLUG
	}
#endif

	return IRQ_HANDLED;
}

static int fpc1020_request_named_gpio(struct fpc1020_data *fpc1020,
				      const char *label, int *gpio)
{
	struct device *dev = fpc1020->dev;
	struct device_node *np = dev->of_node;
	int rc = of_get_named_gpio(np, label, 0);
	if (rc < 0) {
		dev_err(dev, "failed to get '%s'\n", label);
		return rc;
	}
	*gpio = rc;
	rc = devm_gpio_request(dev, *gpio, label);
	if (rc) {
		dev_err(dev, "failed to request gpio %d\n", *gpio);
		return rc;
	}
	dev_dbg(dev, "%s %d\n", label, *gpio);
	return 0;
}

static int fpc1020_probe(struct platform_device* pdev)
{
	struct device *dev = &pdev->dev;
	int rc = 0;
	int irqf;
	struct device_node *np = dev->of_node;
	struct fpc1020_data *fpc1020;

	if (kenzo_fpsensor != 1) {
		pr_err("board no fpc fpsensor\n");
		return -ENODEV;
	}

	fpc1020 = devm_kzalloc(dev, sizeof(*fpc1020),
						    GFP_KERNEL);
	if (!fpc1020) {
		dev_err(dev,
			"failed to allocate memory for struct fpc1020_data\n");
		rc = -ENOMEM;
		goto exit;
	}

	fpc1020->dev = dev;
	dev_set_drvdata(dev, fpc1020);

	if (!np) {
		dev_err(dev, "no of node found\n");
		rc = -EINVAL;
		goto exit;
	}

	rc = fpc1020_request_named_gpio(fpc1020, "fpc,gpio_irq",
					&fpc1020->irq_gpio);
	if (rc)
		goto exit;
	rc = fpc1020_request_named_gpio(fpc1020, "fpc,gpio_rst",
					&fpc1020->rst_gpio);
	if (rc)
		goto exit;


	rc = vreg_setup(fpc1020, "vcc_spi", true);
	if (rc)
		goto exit;


	rc = gpio_direction_input(fpc1020->irq_gpio);
	if (rc)
		goto exit;

	rc = gpio_direction_output(fpc1020->rst_gpio, 1);
	if (rc)
		goto exit;

	fpc1020->wakeup_enabled = false;

	rc = fpc1020_input_init(fpc1020);
	if (rc)
		goto exit;

	irqf = IRQF_TRIGGER_RISING | IRQF_ONESHOT;
	if (of_property_read_bool(dev->of_node, "fpc,enable-wakeup")) {
		irqf |= IRQF_NO_SUSPEND;
		device_init_wakeup(dev, 1);
	}
	mutex_init(&fpc1020->lock);
	rc = devm_request_threaded_irq(dev, gpio_to_irq(fpc1020->irq_gpio),
				       NULL, fpc1020_irq_handler, irqf,
				       dev_name(dev), fpc1020);
	if (rc) {
		dev_err(dev, "could not request irq %d\n",
			gpio_to_irq(fpc1020->irq_gpio));
		goto exit;
	}
	dev_dbg(dev, "requested irq %d\n", gpio_to_irq(fpc1020->irq_gpio));

	/* Request that the interrupt should be wakeable */
	enable_irq_wake(gpio_to_irq(fpc1020->irq_gpio));

	wake_lock_init(&fpc1020->ttw_wl, WAKE_LOCK_SUSPEND, "fpc_ttw_wl");

	hw_reset(fpc1020);

	rc = sysfs_create_group(&dev->kobj, &attribute_group);
	if (rc) {
		dev_err(dev, "could not create sysfs\n");
		goto exit;
	}

	dev_info(dev, "%s: ok\n", __func__);
exit:
	return rc;
}

static int fpc1020_remove(struct platform_device* pdev)
{
	struct fpc1020_data *fpc1020 = dev_get_drvdata(&pdev->dev);

	if (fpc1020->input_dev != NULL)
		input_free_device(fpc1020->input_dev);

	sysfs_remove_group(&fpc1020->dev->kobj, &attribute_group);
	mutex_destroy(&fpc1020->lock);
	wake_lock_destroy(&fpc1020->ttw_wl);
	(void)vreg_setup(fpc1020, "vcc_spi", false);
	dev_info(fpc1020->dev, "%s\n", __func__);
	return 0;
}

static int fpc1020_suspend(struct platform_device* pdev, pm_message_t mesg)
{
	return 0;
}

static int fpc1020_resume(struct platform_device* pdev)
{
	return 0;
}
static struct of_device_id fpc1020_of_match[] = {
	{.compatible = "fpc,fpc1020",},
	{}
};

MODULE_DEVICE_TABLE(of, fpc1020_of_match);

static struct platform_driver fpc1020_driver = {
	.driver = {
		   .name = "fpc1020",
		   .owner = THIS_MODULE,
		   .of_match_table = fpc1020_of_match,
		   },
	.probe = fpc1020_probe,
	.remove = fpc1020_remove,
	.suspend = fpc1020_suspend,
	.resume = fpc1020_resume,
};

static int __init fpc1020_init(void)
{
	int rc = platform_driver_register(&fpc1020_driver);
	if (!rc)
		pr_info("%s OK\n", __func__);
	else
		pr_err("%s %d\n", __func__, rc);
	return rc;
}

static void __exit fpc1020_exit(void)
{
	pr_info("%s\n", __func__);
	platform_driver_unregister(&fpc1020_driver);
}

module_init(fpc1020_init);
module_exit(fpc1020_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Aleksej Makarov");
MODULE_AUTHOR("Henrik Tillman <henrik.tillman@fingerprints.com>");
MODULE_DESCRIPTION("FPC1020 Fingerprint sensor device driver.");
