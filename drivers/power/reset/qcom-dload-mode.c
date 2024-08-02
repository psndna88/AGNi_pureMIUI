// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020 The Linux Foundation. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/pm.h>
#include <linux/qcom_scm.h>
#include <soc/qcom/minidump.h>

enum qcom_download_dest {
	QCOM_DOWNLOAD_DEST_UNKNOWN = -1,
	QCOM_DOWNLOAD_DEST_QPST = 0,
	QCOM_DOWNLOAD_DEST_EMMC = 2,
};

struct qcom_dload {
	struct notifier_block panic_nb;
	struct notifier_block reboot_nb;
	struct kobject kobj;

	bool in_panic;
	void __iomem *dload_dest_addr;
};

#define to_qcom_dload(o) container_of(o, struct qcom_dload, kobj)

#define QCOM_DOWNLOAD_BOTHDUMP (QCOM_DOWNLOAD_FULLDUMP | QCOM_DOWNLOAD_MINIDUMP)

static bool enable_dump =
	IS_ENABLED(CONFIG_POWER_RESET_QCOM_DOWNLOAD_MODE_DEFAULT);
static enum qcom_download_mode current_download_mode = QCOM_DOWNLOAD_NODUMP;
static enum qcom_download_mode dump_mode = QCOM_DOWNLOAD_BOTHDUMP;
static bool early_pcie_init_enable;

static int set_download_mode(enum qcom_download_mode mode)
{
	if ((mode & QCOM_DOWNLOAD_MINIDUMP) && !msm_minidump_enabled()) {
		mode &= ~QCOM_DOWNLOAD_MINIDUMP;
		pr_warn("Minidump not enabled.\n");
		if (!mode)
			return -ENODEV;
	}
	current_download_mode = mode;
	qcom_scm_set_download_mode(mode, 0);
	return 0;
}

static int set_dump_mode(enum qcom_download_mode mode)
{
	int ret = 0;

	if (enable_dump) {
		ret = set_download_mode(mode);
		if (likely(!ret))
			dump_mode = mode;
	} else
		dump_mode = mode;
	return ret;
}

static void msm_enable_dump_mode(bool enable)
{
	if (enable)
		set_download_mode(dump_mode);
	else
		set_download_mode(QCOM_DOWNLOAD_NODUMP);
}

static void set_download_dest(struct qcom_dload *poweroff,
			      enum qcom_download_dest dest)
{
	if (poweroff->dload_dest_addr)
		__raw_writel(dest, poweroff->dload_dest_addr);
}
static enum qcom_download_dest get_download_dest(struct qcom_dload *poweroff)
{
	if (poweroff->dload_dest_addr)
		return __raw_readl(poweroff->dload_dest_addr);
	else
		return QCOM_DOWNLOAD_DEST_UNKNOWN;
}

static int param_set_download_mode(const char *val,
		const struct kernel_param *kp)
{
	int ret;

	/* update enable_dump according to user input */
	ret = param_set_bool(val, kp);
	if (ret)
		return ret;

	msm_enable_dump_mode(true);

	return 0;
}
module_param_call(download_mode, param_set_download_mode, param_get_int,
			&enable_dump, 0644);

/* interface for exporting attributes */
struct reset_attribute {
	struct attribute        attr;
	ssize_t (*show)(struct kobject *kobj, struct attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct attribute *attr,
			const char *buf, size_t count);
};
#define to_reset_attr(_attr) \
	container_of(_attr, struct reset_attribute, attr)

static ssize_t attr_show(struct kobject *kobj, struct attribute *attr,
				char *buf)
{
	struct reset_attribute *reset_attr = to_reset_attr(attr);
	ssize_t ret = -EIO;

	if (reset_attr->show)
		ret = reset_attr->show(kobj, attr, buf);

	return ret;
}

static ssize_t attr_store(struct kobject *kobj, struct attribute *attr,
				const char *buf, size_t count)
{
	struct reset_attribute *reset_attr = to_reset_attr(attr);
	ssize_t ret = -EIO;

	if (reset_attr->store)
		ret = reset_attr->store(kobj, attr, buf, count);

	return ret;
}

static const struct sysfs_ops reset_sysfs_ops = {
	.show	= attr_show,
	.store	= attr_store,
};

static struct kobj_type qcom_dload_kobj_type = {
	.sysfs_ops	= &reset_sysfs_ops,
};

static ssize_t emmc_dload_show(struct kobject *kobj,
			       struct attribute *this,
			       char *buf)
{
	struct qcom_dload *poweroff = to_qcom_dload(kobj);

	if (!poweroff->dload_dest_addr)
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			get_download_dest(poweroff) == QCOM_DOWNLOAD_DEST_EMMC);
}
static ssize_t emmc_dload_store(struct kobject *kobj,
				struct attribute *this,
				const char *buf, size_t count)
{
	int ret;
	bool enabled;
	struct qcom_dload *poweroff = to_qcom_dload(kobj);

	if (!poweroff->dload_dest_addr)
		return -ENODEV;

	ret = kstrtobool(buf, &enabled);

	if (ret < 0)
		return ret;

	if (enabled)
		set_download_dest(poweroff, QCOM_DOWNLOAD_DEST_EMMC);
	else
		set_download_dest(poweroff, QCOM_DOWNLOAD_DEST_QPST);

	return count;
}
static struct reset_attribute attr_emmc_dload = __ATTR_RW(emmc_dload);

static ssize_t dload_mode_show(struct kobject *kobj,
			       struct attribute *this,
			       char *buf)
{
	const char *mode;

	switch ((unsigned int)dump_mode) {
	case QCOM_DOWNLOAD_FULLDUMP:
		mode = "full";
		break;
	case QCOM_DOWNLOAD_MINIDUMP:
		mode = "mini";
		break;
	case QCOM_DOWNLOAD_BOTHDUMP:
		mode = "both";
		break;
#ifdef CONFIG_POWER_RESET_QCOM_DOWNLOAD_MODE_NODUMP
	case QCOM_DOWNLOAD_NODUMP:
		mode = "nodump";
		break;
#endif
	default:
		mode = "unknown";
		break;
	}
	return scnprintf(buf, PAGE_SIZE, "DLOAD dump type: %s\n", mode);
}
static ssize_t dload_mode_store(struct kobject *kobj,
				struct attribute *this,
				const char *buf, size_t count)
{
	enum qcom_download_mode mode;
#ifdef CONFIG_POWER_RESET_QCOM_DOWNLOAD_MODE_NODUMP
	int temp;

	dump_mode = qcom_scm_get_download_mode(&temp, 0) ? dump_mode : temp;

	if (dump_mode == QCOM_DOWNLOAD_NODUMP) {
		pr_err("%s: Current dump mode already set: nodump\n", __func__);
		pr_err("%s: Changing dump mode now is not allowed, reboot the device\n", __func__);
		return -EINVAL;
	}
#endif

	if (sysfs_streq(buf, "full"))
		mode = QCOM_DOWNLOAD_FULLDUMP;
	else if (sysfs_streq(buf, "mini"))
		mode = QCOM_DOWNLOAD_MINIDUMP;
	else if (sysfs_streq(buf, "both"))
		mode = QCOM_DOWNLOAD_BOTHDUMP;
#ifdef CONFIG_POWER_RESET_QCOM_DOWNLOAD_MODE_NODUMP
	else if (sysfs_streq(buf, "nodump")) {
		mode = QCOM_DOWNLOAD_NODUMP;
		qcom_scm_disable_sdi();
	}
#endif
	else {
		pr_err("Invalid dump mode request...\n");
#ifdef CONFIG_POWER_RESET_QCOM_DOWNLOAD_MODE_NODUMP
		pr_err("Supported dumps: 'full', 'mini', 'both' or 'nodump'\n");
#else
		pr_err("Supported dumps: 'full', 'mini', or 'both'\n");
#endif
		return -EINVAL;
	}

	return set_dump_mode(mode) ? : count;
}
static struct reset_attribute attr_dload_mode = __ATTR_RW(dload_mode);

static struct attribute *qcom_dload_attrs[] = {
	&attr_emmc_dload.attr,
	&attr_dload_mode.attr,
	NULL
};
static struct attribute_group qcom_dload_attr_group = {
	.attrs = qcom_dload_attrs,
};

static int qcom_dload_panic(struct notifier_block *this, unsigned long event,
			      void *ptr)
{
	struct qcom_dload *poweroff = container_of(this, struct qcom_dload,
						     panic_nb);
	poweroff->in_panic = true;
	if (enable_dump)
		msm_enable_dump_mode(true);
	return NOTIFY_OK;
}

static int qcom_dload_reboot(struct notifier_block *this, unsigned long event,
			      void *ptr)
{
	char *cmd = ptr;
	struct qcom_dload *poweroff = container_of(this, struct qcom_dload,
						     reboot_nb);

	/* Clean shutdown, disable dump mode to allow normal restart */
	if (!poweroff->in_panic)
		set_download_mode(QCOM_DOWNLOAD_NODUMP);

	if (cmd) {
		if (!strcmp(cmd, "qcom_dload"))
			msm_enable_dump_mode(true);
	}

	if (current_download_mode != QCOM_DOWNLOAD_NODUMP)
		reboot_mode = REBOOT_WARM;

	return NOTIFY_OK;
}

static void __iomem *map_prop_mem(const char *propname)
{
	struct device_node *np = of_find_compatible_node(NULL, NULL, propname);
	void __iomem *addr;

	if (!np) {
		pr_err("Unable to find DT property: %s\n", propname);
		return NULL;
	}

	addr = of_iomap(np, 0);
	if (!addr)
		pr_err("Unable to map memory for DT property: %s\n", propname);
	return addr;
}

#ifdef CONFIG_RANDOMIZE_BASE
#define KASLR_OFFSET_MASK	0x00000000FFFFFFFF
static void store_kaslr_offset(void)
{
	void __iomem *mem = map_prop_mem("qcom,msm-imem-kaslr_offset");

	if (!mem)
		return;

	__raw_writel(0xdead4ead, mem);
	__raw_writel((kimage_vaddr - KIMAGE_VADDR) & KASLR_OFFSET_MASK,
		     mem + 4);
	__raw_writel(((kimage_vaddr - KIMAGE_VADDR) >> 32) & KASLR_OFFSET_MASK,
		     mem + 8);

	iounmap(mem);
}
#else
static void store_kaslr_offset(void) {}
#endif /* CONFIG_RANDOMIZE_BASE */

static void check_pci_edl(struct device_node *np)
{
	void __iomem *mem;
	uint32_t read_val;
	int ret_l, ret_h, l, h, mask_value;

	mem = of_iomap(np, 0);
	if (!mem) {
		pr_info("Unable to map memory for DT property: %s\n", np->name);
		return;
	}

	read_val = __raw_readl(mem);
	ret_l = of_property_read_u32_index(np, "qcom,boot-config-shift", 0, &l);
	ret_h = of_property_read_u32_index(np, "qcom,boot-config-shift", 1, &h);

	if (!ret_l && !ret_h) {
		mask_value = (read_val >> l) & GENMASK(h - l, 0);
		if (mask_value == 5 || mask_value == 7) {
			early_pcie_init_enable = true;
			pr_info("Setting up EDL mode to PCIE\n");
		}
	}

	iounmap(mem);
}

#define DISPLAY_CONFIG_OFFSET_PROP "qcom,msm-imem-display_config_offset"
/*
 ** set display config imem first 4 bytes to 0xdead4ead, because imem context
 ** will not lost when warm reset. if panic, xbl ramdump will display orange
 ** screen, and framebuffer addr is determined by these four bytes in
 ** MDP_GetDisplayBootConfig function. so set these four bytes to a invalid
 ** value and let the framebuffer of orange screen use
 ** RAMDUMP_FRAME_BUFFER_ADDRESS(0xE1000000)
 **/
static void clear_display_config(void)
{
	void *display_config_imem_addr = map_prop_mem(DISPLAY_CONFIG_OFFSET_PROP);

	if (display_config_imem_addr) {
		__raw_writel(0xdead4ead, display_config_imem_addr);
		iounmap(display_config_imem_addr);
		pr_err("%s clear display config\n", __func__);
	}
}

static int qcom_dload_probe(struct platform_device *pdev)
{
	struct qcom_dload *poweroff;
	int ret;

	if (!qcom_scm_is_available())
		return -EPROBE_DEFER;

	poweroff = devm_kzalloc(&pdev->dev, sizeof(*poweroff), GFP_KERNEL);
	if (!poweroff)
		return -ENOMEM;

	ret = kobject_init_and_add(&poweroff->kobj, &qcom_dload_kobj_type,
				   kernel_kobj, "dload");
	if (ret) {
		pr_err("%s: Error in creation kobject_add\n", __func__);
		kobject_put(&poweroff->kobj);
		return ret;
	}

	ret = sysfs_create_group(&poweroff->kobj, &qcom_dload_attr_group);
	if (ret) {
		pr_err("%s: Error in creation sysfs_create_group\n", __func__);
		kobject_del(&poweroff->kobj);
		return ret;
	}

	poweroff->dload_dest_addr = map_prop_mem("qcom,msm-imem-dload-type");
	store_kaslr_offset();
	check_pci_edl(pdev->dev.of_node);
	clear_display_config();

	msm_enable_dump_mode(enable_dump);
	if (!enable_dump)
		qcom_scm_disable_sdi();

	poweroff->panic_nb.notifier_call = qcom_dload_panic;
	poweroff->panic_nb.priority = INT_MAX;
	atomic_notifier_chain_register(&panic_notifier_list,
				       &poweroff->panic_nb);

	poweroff->reboot_nb.notifier_call = qcom_dload_reboot;
	poweroff->reboot_nb.priority = 255;
	register_reboot_notifier(&poweroff->reboot_nb);

	platform_set_drvdata(pdev, poweroff);

	return 0;
}

static int qcom_dload_remove(struct platform_device *pdev)
{
	struct qcom_dload *poweroff = platform_get_drvdata(pdev);

	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &poweroff->panic_nb);
	unregister_reboot_notifier(&poweroff->reboot_nb);

	if (poweroff->dload_dest_addr)
		iounmap(poweroff->dload_dest_addr);

	return 0;
}

static const struct of_device_id of_qcom_dload_match[] = {
	{ .compatible = "qcom,dload-mode", },
	{},
};
MODULE_DEVICE_TABLE(of, of_qcom_dload_match);

static struct platform_driver qcom_dload_driver = {
	.probe = qcom_dload_probe,
	.remove = qcom_dload_remove,
	.driver = {
		.name = "qcom-dload-mode",
		.of_match_table = of_match_ptr(of_qcom_dload_match),
	},
};

static int __init qcom_dload_driver_init(void)
{
	return platform_driver_register(&qcom_dload_driver);
}
#if IS_MODULE(CONFIG_POWER_RESET_QCOM_DOWNLOAD_MODE)
module_init(qcom_dload_driver_init);
#else
pure_initcall(qcom_dload_driver_init);
#endif

static void __exit qcom_dload_driver_exit(void)
{
	return platform_driver_unregister(&qcom_dload_driver);
}
module_exit(qcom_dload_driver_exit);

MODULE_DESCRIPTION("MSM Download Mode Driver");
MODULE_LICENSE("GPL v2");
