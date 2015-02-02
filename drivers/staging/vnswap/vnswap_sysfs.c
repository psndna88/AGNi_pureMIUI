/*
 * Virtual Nand Swap Device which simulates Swap Area
 *
 * Copyright (C) 2013 SungHwan Yun
 *
 * This code is released using a dual license strategy: BSD/GPL
 * You can choose the licence that better fits your requirements.
 *
 * Released under the terms of 3-clause BSD License
 * Released under the terms of GNU General Public License Version 2.0
 */

#include <linux/device.h>
#include <linux/genhd.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/atomic.h>
#include <linux/types.h>

#include "vnswap.h"

static ssize_t disksize_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%llu\n", vnswap_device->disksize);
}

static ssize_t disksize_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	int ret;
	u64 disksize;

	ret = kstrtoull(buf, 10, &disksize);
	if (ret)
		return ret;

	vnswap_init_disksize(disksize);
	return len;
}

static ssize_t swap_filename_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	if (!vnswap_device)
		return 0;
	dprintk("%s %d: backing_storage_filename = %s\n",
			__func__, __LINE__,
			vnswap_device->backing_storage_filename);
	return sprintf(buf, "%s\n", vnswap_device->backing_storage_filename);
}

static ssize_t swap_filename_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	if (!vnswap_device) {
		pr_err("%s %d: vnswap_device is null\n", __func__, __LINE__);
		return len;
	}
	memcpy((void *)vnswap_device->backing_storage_filename,
			(void *)buf, len);
	dprintk("%s %d: (buf, len, backing_storage_filename) = " \
			"(%s, %d, %s)\n",
			__func__, __LINE__,
			buf, len, vnswap_device->backing_storage_filename);
	return len;
}

static ssize_t init_backing_storage_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "(disksize, bs_size) = (%llu, %llu)\n",
		vnswap_device ? vnswap_device->disksize : 0,
		vnswap_device ? vnswap_device->bs_size : 0);
}

static ssize_t init_backing_storage_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	vnswap_init_backing_storage();
	return len;
}

static ssize_t vnswap_init_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "(vnswap_is_init, init_success) = (%llu, %d)\n",
			vnswap_device->stats.vnswap_is_init,
			(vnswap_device) ? vnswap_device->init_success : 0);
}

static ssize_t vnswap_swap_info_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "(%d, %d, %d) (%llu, %d, %d, %d, %d, %d) " \
						"(%d, %d, %d, %d, %d, %d, " \
						"%d, %d, %d, %d, %d, %d)\n",
		vnswap_device->stats.vnswap_stored_pages.counter,
		vnswap_device->stats.vnswap_write_pages.counter,
		vnswap_device->stats.vnswap_read_pages.counter,
		vnswap_device->stats.vnswap_total_slot_num,
		vnswap_device->stats.vnswap_used_slot_num.counter,
		vnswap_device->stats.vnswap_backing_storage_full_num.counter,
		vnswap_device->stats.vnswap_mapped_slot_free_num.counter,
		vnswap_device->stats.vnswap_not_mapped_slot_free_num.counter,
		vnswap_device->stats.vnswap_double_mapped_slot_num.counter,
		vnswap_device->stats.vnswap_bio_end_fail_r1_num.counter,
		vnswap_device->stats.vnswap_bio_end_fail_r2_num.counter,
		vnswap_device->stats.vnswap_bio_end_fail_r3_num.counter,
		vnswap_device->stats.vnswap_bio_end_fail_w1_num.counter,
		vnswap_device->stats.vnswap_bio_end_fail_w2_num.counter,
		vnswap_device->stats.vnswap_bio_end_fail_w3_num.counter,
		vnswap_device->stats.vnswap_bio_large_bi_size_num.counter,
		vnswap_device->stats.vnswap_bio_large_bi_vcnt_num.counter,
		vnswap_device->stats.vnswap_bio_invalid_num.counter,
		vnswap_device->stats.vnswap_bio_no_mem_num.counter,
		vnswap_device->stats.vnswap_not_mapped_read_pages.counter,
		vnswap_device->stats.vnswap_backing_storage_open_fail
	);
}

static DEVICE_ATTR(disksize, S_IRUGO | S_IWUSR, disksize_show,
	disksize_store);
static DEVICE_ATTR(swap_filename, S_IRUGO | S_IWUSR, swap_filename_show,
	swap_filename_store);
static DEVICE_ATTR(init_backing_storage, S_IRUGO | S_IWUSR,
	init_backing_storage_show, init_backing_storage_store);
static DEVICE_ATTR(vnswap_init, S_IRUGO | S_IWUSR,
	vnswap_init_show, NULL);
static DEVICE_ATTR(vnswap_swap_info, S_IRUGO | S_IWUSR,
	vnswap_swap_info_show, NULL);

static struct attribute *vnswap_disk_attrs[] = {
	&dev_attr_disksize.attr,
	&dev_attr_swap_filename.attr,
	&dev_attr_init_backing_storage.attr,
	&dev_attr_vnswap_init.attr,
	&dev_attr_vnswap_swap_info.attr,
	NULL,
};

struct attribute_group vnswap_disk_attr_group = {
	.attrs = vnswap_disk_attrs,
};
