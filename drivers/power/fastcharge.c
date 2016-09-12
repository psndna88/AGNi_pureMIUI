/*
 *
 * USB Fastcharge
 *
 *   0 - disabled (default)
 *   1 - increase charge current limit to 900mA
 *
*/

#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>

int force_fast_charge = 0;

static ssize_t force_fast_charge_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	size_t count = 0;
	count += sprintf(buf, "%d\n", force_fast_charge);
	return count;
}

static ssize_t force_fast_charge_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	if (buf[0] >= '0' && buf[0] <= '1' && buf[1] == '\n')
                if (force_fast_charge != buf[0] - '0')
		        force_fast_charge = buf[0] - '0';

	return count;
}

static struct kobj_attribute force_fast_charge_attribute =
__ATTR(force_fast_charge, 0666, force_fast_charge_show, force_fast_charge_store);

static struct attribute *force_fast_charge_attrs[] = {
	&force_fast_charge_attribute.attr,
	NULL,
};

static struct attribute_group force_fast_charge_attr_group = {
	.attrs = force_fast_charge_attrs,
};

static struct kobject *force_fast_charge_kobj;

int force_fast_charge_init(void)
{
	int ret;

	force_fast_charge_kobj = kobject_create_and_add("fast_charge", kernel_kobj);
	if (!force_fast_charge_kobj) {
			return -ENOMEM;
	}

	ret = sysfs_create_group(force_fast_charge_kobj, &force_fast_charge_attr_group);

	if (ret)
		kobject_put(force_fast_charge_kobj);

	return ret;
}


module_init(force_fast_charge_init);

