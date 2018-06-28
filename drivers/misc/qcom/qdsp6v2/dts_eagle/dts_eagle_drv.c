/* DTS_EAGLE START */

#include <linux/module.h>   /* Needed by all modules */
#include <linux/kernel.h>   /* Needed for KERN_INFO */
#include <linux/init.h>     /* Needed for the macros */
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/semaphore.h>
#include <linux/errno.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include "dts_eagle_drv.h"

#define DTS_EAGLE_DRIVER_FIRST_MINOR    1
#define DTS_EAGLE_DRIVER_MINOR_CNT      6

enum {
	AUDIO_DEVICE_OUT_EARPIECE = 0x1,
	AUDIO_DEVICE_OUT_SPEAKER = 0x2,
	AUDIO_DEVICE_OUT_WIRED_HEADSET = 0x4,
	AUDIO_DEVICE_OUT_WIRED_HEADPHONE = 0x8,
	AUDIO_DEVICE_OUT_BLUETOOTH_SCO = 0x10,
	AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET = 0x20,
	AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT = 0x40,
	AUDIO_DEVICE_OUT_BLUETOOTH_A2DP = 0x80,
	AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES = 0x100,
	AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER = 0x200,
    AUDIO_DEVICE_OUT_USB_DEVICE = 0x4000
};
#define AUDIO_DEVICE_COMBO 0x400000 /* bit 23 */
#define DEVICE_OUT_ALL_BLUETOOTH (AUDIO_DEVICE_OUT_BLUETOOTH_SCO | AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET | \
				  AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT | AUDIO_DEVICE_OUT_BLUETOOTH_A2DP | \
				  AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES | AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER)

enum {                  /* cache block */
	CB_0 = 0,
	CB_1,
	CB_2,
	CB_3,
	CB_4,
	CB_5,
	CB_6,
	CB_7,
	CB_COUNT
};

enum {                  /* cache block description */
	CBD_DEV_MASK = 0,
	CBD_OFFSG,
	CBD_CMD0,
	CBD_SZ0,
	CBD_OFFS1,
	CBD_CMD1,
	CBD_SZ1,
	CBD_OFFS2,
	CBD_CMD2,
	CBD_SZ2,
	CBD_OFFS3,
	CBD_CMD3,
	CBD_SZ3,
	CBD_SR,
	CBD_COUNT,
};

/* pr_err */
#define dts_eagle_drv_err_msg(fmt, ...)  \
	(printk(KERN_INFO "DTS_EAGLE_DRIVER: " fmt "\n", ##__VA_ARGS__))

/* dts eagle driver */
static dev_t            dts_eagle_dev;
static struct   cdev    dts_eagle_char_dev;
static struct   class   *p_dts_eagle_class;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37)
static DECLARE_MUTEX(lock);
#else
static DEFINE_SEMAPHORE(lock);
#endif

/* dts eagle parameter cache */
#define DEPC_MAX_SIZE 524288

static int _ref_cnt;
static char *_depc;
static u32 _depc_size;
static s32 _c_bl[CB_COUNT][CBD_COUNT];
static u32 _device_primary;
static u32 _device_all;


/*
 * Internal functions
 */
static void _init_cb_descs(void)
{
	int i;

	for (i = 0; i < CB_COUNT; i++) {
		_c_bl[i][CBD_DEV_MASK] = 0;

		_c_bl[i][CBD_OFFSG] = _c_bl[i][CBD_OFFS1] =
		_c_bl[i][CBD_OFFS2] = _c_bl[i][CBD_OFFS3] = 0xFFFFFFFF;

		_c_bl[i][CBD_CMD0] = _c_bl[i][CBD_SZ0] =
		_c_bl[i][CBD_CMD1] = _c_bl[i][CBD_SZ1] =
		_c_bl[i][CBD_CMD2] = _c_bl[i][CBD_SZ2] =
		_c_bl[i][CBD_CMD3] = _c_bl[i][CBD_SZ3] = 0;

		_c_bl[i][CBD_SR] = 48000;
	}
}


static s32 _get_cb_for_dev(int device, unsigned int rate )
{
	s32 i;
	const int multi_rate_devices = DEVICE_OUT_ALL_BLUETOOTH|AUDIO_DEVICE_OUT_USB_DEVICE;

	if (device & AUDIO_DEVICE_COMBO) {
		for (i = 0; i < CB_COUNT; i++) {
			if ((_c_bl[i][CBD_DEV_MASK] & device) == device)
				return i;
		}
	} else {
		for (i = 0; i < CB_COUNT; i++) {
			int cb_dev = _c_bl[i][CBD_DEV_MASK];
			if ((cb_dev & device) && !(cb_dev & AUDIO_DEVICE_COMBO)) {
				if (cb_dev & multi_rate_devices) {
					if (_c_bl[i][CBD_SR] == rate) {
						return i;
					} else {
						continue;
					}
				} else {
					return i;
				}
			}
		}
	}
	dts_eagle_drv_err_msg("%s: device %i not found", __func__, device);
	return -EINVAL;
}

/*
 * Driver functions
 */
static int dts_eagle_open(struct inode *i, struct file *f)
{
	dts_eagle_drv_dbg_msg("%s", __func__);
	return 0;
}

static int dts_eagle_close(struct inode *i, struct file *f)
{
	dts_eagle_drv_dbg_msg("%s", __func__);
	return 0;
}

static long dts_eagle_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	down(&lock);
	switch (cmd) {
	case DTS_EAGLE_IOCTL_GET_CACHE_SIZE: {
		dts_eagle_drv_dbg_msg("%s: called with control 0x%X (get param cache size)",
			 __func__, cmd);

		if (copy_to_user((void *)arg, &_depc_size, sizeof(_depc_size))) {
			dts_eagle_drv_err_msg("%s: error writing size", __func__);
			up(&lock);
			return -EFAULT;
		}
		break;
	}

	case DTS_EAGLE_IOCTL_SET_CACHE_SIZE: {
		s32 size = 0;

		dts_eagle_drv_dbg_msg("%s: called with control 0x%X (allocate param cache)",
			 __func__, cmd);

		if (copy_from_user((void *)&size, (void *)arg, sizeof(size))) {
			dts_eagle_drv_err_msg("%s: error copying size (src:%p, tgt:%p, size:%zu)",
				__func__, (void *)arg, &size, sizeof(size));
			up(&lock);
			return -EFAULT;
		} else if (size < 0 || size > DEPC_MAX_SIZE) {
			dts_eagle_drv_err_msg("%s: cache size %d not allowed (min 0, max %d)",
				 __func__, size, DEPC_MAX_SIZE);
			up(&lock);
			return -EINVAL;
		}

		if (_depc) {
			dts_eagle_drv_dbg_msg("%s: previous param cache of size %u freed",
				 __func__, _depc_size);
			_depc_size = 0;
			kfree(_depc);
			_depc = NULL;
		}

		if (size)
			_depc = kzalloc(size, GFP_KERNEL);
		else
			dts_eagle_drv_dbg_msg("%s: %d bytes requested for param cache, nothing allocated",
				 __func__, size);

		if (_depc) {
			dts_eagle_drv_dbg_msg("%s: %d bytes allocated for param cache",
				 __func__, size);
			_depc_size = size;
		} else {
			dts_eagle_drv_err_msg("%s: error allocating param cache (kzalloc failed on %d bytes)",
				__func__, size);
			_depc_size = 0;
			up(&lock);
			return -ENOMEM;
		}
		break;
	}

	case DTS_EAGLE_IOCTL_GET_PARAM: {
		struct dts_eagle_param_desc depd;
		s32 offset = 0, cb = 0;
		void *buf = NULL;

		dts_eagle_drv_dbg_msg("%s: control 0x%X (get param)",
			__func__, cmd);

		if (copy_from_user((void *)&depd, (void *)arg, sizeof(depd))) {
			dts_eagle_drv_err_msg("%s: error copying dts_eagle_param_desc (src:%p, tgt:%p, size:%zu)",
				__func__, (void *)arg, &depd, sizeof(depd));
			up(&lock);
			return -EFAULT;
		}

		depd.device &= DTS_EAGLE_FLAG_IOCTL_MASK;
		cb = _get_cb_for_dev(depd.device, depd.rate);
		if (cb < 0) {
			dts_eagle_drv_err_msg("%s: no cache for device %i found",
				 __func__, depd.device);
			up(&lock);
			return -EINVAL;
		}

		offset = _c_bl[cb][CBD_OFFSG] + depd.offset;
		if ((offset + depd.size) > _depc_size) {
			dts_eagle_drv_err_msg("%s: invalid size %d and/or offset %d",
				 __func__,
			     depd.size, offset);
			up(&lock);
			return -EINVAL;
		}
		buf = (void *)&_depc[offset];

		if (copy_to_user((void *)(((char *)arg) + sizeof(depd)),
		     buf, depd.size)) {
			dts_eagle_drv_err_msg("%s: error copying get data",
				__func__);
			up(&lock);
			return -EFAULT;
		}
		break;
	}

	case DTS_EAGLE_IOCTL_SET_PARAM: {
		struct dts_eagle_param_desc depd;
		s32 offset = 0, just_set_cache = 0, for_pre = 0;
		s32 tgt;

		dts_eagle_drv_dbg_msg("%s: control 0x%X (set param)",
			__func__, cmd);

		if (copy_from_user((void *)&depd, (void *)arg, sizeof(depd))) {
			dts_eagle_drv_err_msg("%s: error copying dts_eagle_param_desc (src:%p, tgt:%p, size:%zu)",
				__func__, (void *)arg, &depd, sizeof(depd));
			up(&lock);
			return -EFAULT;
		}

		if (depd.device & DTS_EAGLE_FLAG_IOCTL_PRE) {
			dts_eagle_drv_dbg_msg("%s: using for premix", __func__);
			for_pre = 1;
		}

		if (depd.device & DTS_EAGLE_FLAG_IOCTL_JUSTSETCACHE) {
			dts_eagle_drv_dbg_msg("%s: 'just set cache' requested",
				__func__);
			just_set_cache = 1;
		}

		depd.device &= DTS_EAGLE_FLAG_IOCTL_MASK;
		tgt = _get_cb_for_dev(depd.device, depd.rate);
		if (tgt < 0) {
			dts_eagle_drv_err_msg("%s: no cache for device %i found",
				 __func__, depd.device);
			up(&lock);
			return -EINVAL;
		}

		offset = _c_bl[tgt][CBD_OFFSG] + depd.offset;
		if ((offset + depd.size) > _depc_size) {
			dts_eagle_drv_err_msg("%s: invalid size %i and/or offset %i for parameter (target cache block %i with offset %i, global cache is size %u)",
				 __func__, depd.size, offset, tgt,
				 _c_bl[tgt][CBD_OFFSG], _depc_size);
			up(&lock);
			return -EINVAL;
		}

		if (copy_from_user((void *)&_depc[offset],
		     (void *)(((char *)arg)+sizeof(depd)), depd.size)) {
			dts_eagle_drv_err_msg("%s: error copying param to cache (src:%p, tgt:%p, size:%i)",
				 __func__, ((char *)arg)+sizeof(depd),
				 &_depc[offset], depd.size);
			up(&lock);
			return -EFAULT;
		}

		dts_eagle_drv_dbg_msg("%s: param info: param = 0x%X, size = %i, offset = %i, device = %i, cache block %i, global offset = %i, first bytes as integer = %i",
			__func__, depd.id, depd.size, depd.offset, depd.device,
			tgt, offset, *(int *)&_depc[offset]);

		break;
	}

	case DTS_EAGLE_IOCTL_SET_CACHE_BLOCK: {
		u32 b_[CBD_COUNT+1], *b = &b_[1], cb;

		dts_eagle_drv_dbg_msg("%s: with control 0x%X (set cache block)",
			 __func__, cmd);

		if (copy_from_user((void *)b_, (void *)arg, sizeof(b_))) {
			dts_eagle_drv_err_msg("%s: error copying cache block data (src:%p, tgt:%p, size:%zu)",
				 __func__, (void *)arg, b_, sizeof(b_));
			up(&lock);
			return -EFAULT;
		}

		cb = b_[0];
		if (cb >= CB_COUNT) {
			dts_eagle_drv_err_msg("%s: cache block %u out of range (max %u)",
				 __func__, cb, CB_COUNT-1);
			up(&lock);
			return -EINVAL;
		}

		dts_eagle_drv_dbg_msg("%s: cache block %i set: devices 0x%X, rate = %u, global offset %u, offsets 1:%u 2:%u 3:%u, cmds/sizes 0:0x%X %u 1:0x%X %u 2:0x%X %u 3:0x%X %u",
			__func__, cb, _c_bl[cb][CBD_DEV_MASK], _c_bl[cb][CBD_SR],
			_c_bl[cb][CBD_OFFSG], _c_bl[cb][CBD_OFFS1],
			_c_bl[cb][CBD_OFFS2], _c_bl[cb][CBD_OFFS3],
			_c_bl[cb][CBD_CMD0], _c_bl[cb][CBD_SZ0],
			_c_bl[cb][CBD_CMD1], _c_bl[cb][CBD_SZ1],
			_c_bl[cb][CBD_CMD2], _c_bl[cb][CBD_SZ2],
			_c_bl[cb][CBD_CMD3], _c_bl[cb][CBD_SZ3]);

		if ((b[CBD_OFFSG]+b[CBD_OFFS1]+b[CBD_SZ1]) > _depc_size ||
		    (b[CBD_OFFSG]+b[CBD_OFFS2]+b[CBD_SZ2]) > _depc_size ||
		    (b[CBD_OFFSG]+b[CBD_OFFS3]+b[CBD_SZ3]) > _depc_size) {
			dts_eagle_drv_err_msg("%s: cache block bounds out of range", __func__);
			up(&lock);
			return -EINVAL;
		}
		memcpy(_c_bl[cb], b, sizeof(_c_bl[cb]));
		break;
	}

	case DTS_EAGLE_IOCTL_SET_ACTIVE_DEVICE: {
		u32 data[2];

		dts_eagle_drv_dbg_msg("%s: with control 0x%X (set active device)",
			 __func__, cmd);

		if (copy_from_user((void *)data, (void *)arg, sizeof(data))) {
			dts_eagle_drv_err_msg("%s: error copying active device data (src:%p, tgt:%p, size:%zu)",
				 __func__, (void *)arg, data, sizeof(data));
			up(&lock);
			return -EFAULT;
		}

		if (data[1] != 0) {
			_device_primary = data[0];
			dts_eagle_drv_dbg_msg("%s: primary device %i",
				__func__, data[0]);
		} else {
			_device_all = data[0];
			dts_eagle_drv_dbg_msg("%s: all devices 0x%X",
				__func__, data[0]);
		}

		break;
	}

	case DTS_EAGLE_IOCTL_GET_CACHE_PREMIX:{
		int offset, cidx = -1, size;
		struct dts_eagle_cache_block pre_cb;

		dts_eagle_drv_dbg_msg("%s: control 0x%X (get param)",
			__func__, cmd);


		if (copy_from_user((void *)&pre_cb, (void *)arg, sizeof(pre_cb))) {
			dts_eagle_drv_err_msg("%s: error copying dts_eagle_cache_block (src:%p, tgt:%p, size:%zu)",
				__func__, (void *)arg, &pre_cb, sizeof(pre_cb));
			up(&lock);
			return -EFAULT;
		}

		cidx = _get_cb_for_dev(_device_primary, pre_cb.rate);
		if (cidx < 0) {
			up(&lock);
			return -EINVAL;
		}

		offset = _c_bl[cidx][CBD_OFFSG];
		cmd = _c_bl[cidx][CBD_CMD0];
		size = _c_bl[cidx][CBD_SZ0] + _c_bl[cidx][CBD_SZ1];

		if (_depc_size == 0 || !_depc || offset < 0 || size <= 0 ||
		     cmd == 0 || (offset + size) > _depc_size) {
			dts_eagle_drv_err_msg("%s: primary device %i cache index %i general error - cache size = %u, cache ptr = %p, offset = %i, size = %i, cmd = %i",
				__func__, _device_primary, cidx, _depc_size,
				_depc, offset, size, cmd);
			up(&lock);
			return -EINVAL;
		}

		dts_eagle_drv_dbg_msg("%s: first 6 integers %i %i %i %i %i %i",
			__func__, *((int *)&_depc[offset]), *((int *)&_depc[offset+4]),
			*((int *)&_depc[offset+8]), *((int *)&_depc[offset+12]),
			*((int *)&_depc[offset+16]), *((int *)&_depc[offset+20]));

		dts_eagle_drv_dbg_msg("%s: sending full data block, with cache index = %d device mask 0x%X, rate = %u, param = 0x%X, offset = %d, and size = %d",
			 __func__, cidx, _c_bl[cidx][CBD_DEV_MASK], _c_bl[cidx][CBD_SR],
			 cmd, offset, size);

		if (copy_to_user(pre_cb.data, &_depc[offset], size)) {
			dts_eagle_drv_err_msg("%s: error copying premix data to userspace",
				 __func__);
			up(&lock);
			return -EFAULT;
		}
		break;
	}

	case DTS_EAGLE_IOCTL_GET_CACHE_POSTMIX: {
		int offset, cidx = -1, size;
		struct dts_eagle_cache_block post_cb;

		dts_eagle_drv_dbg_msg("%s: control 0x%X (get param)",
			__func__, cmd);


		if (copy_from_user((void *)&post_cb, (void *)arg, sizeof(post_cb))) {
			dts_eagle_drv_err_msg("%s: error copying dts_eagle_cache_block (src:%p, tgt:%p, size:%zu)",
				__func__, (void *)arg, &post_cb, sizeof(post_cb));
			up(&lock);
			return -EFAULT;
		}

		cidx = _get_cb_for_dev(_device_primary, post_cb.rate);
		if (cidx < 0) {
			up(&lock);
			return -EINVAL;
		}

		offset = _c_bl[cidx][CBD_OFFSG] + _c_bl[cidx][CBD_OFFS2];
		cmd = _c_bl[cidx][CBD_CMD2];
		size = _c_bl[cidx][CBD_SZ2] + _c_bl[cidx][CBD_SZ3];

		if (_depc_size == 0 || !_depc || offset < 0 || size <= 0 ||
		     cmd == 0 || (offset + size) > _depc_size) {
			dts_eagle_drv_err_msg("%s: primary device %i cache index %i general error - cache size = %u, cache ptr = %p, offset = %i, size = %i, cmd = %i",
				__func__, _device_primary, cidx, _depc_size,
				_depc, offset, size, cmd);
			up(&lock);
			return -EINVAL;
		}

		dts_eagle_drv_dbg_msg("%s: first 6 integers %i %i %i %i %i %i",
			__func__, *((int *)&_depc[offset]), *((int *)&_depc[offset+4]),
			*((int *)&_depc[offset+8]), *((int *)&_depc[offset+12]),
			*((int *)&_depc[offset+16]), *((int *)&_depc[offset+20]));

		dts_eagle_drv_dbg_msg("%s: sending full data block, with cache index = %d device mask 0x%X, rate = %u, param = 0x%X, offset = %d, and size = %d",
			 __func__, cidx, _c_bl[cidx][CBD_DEV_MASK], _c_bl[cidx][CBD_SR],
			cmd, offset, size);

		if (copy_to_user(post_cb.data, &_depc[offset], size)) {
			dts_eagle_drv_err_msg("%s: error copying postmix data to userspace",
				 __func__);
			up(&lock);
			return -EFAULT;
		}
		break;
	}

	default:
		dts_eagle_drv_err_msg("%s: control 0x%X (invalid control)",
			__func__, cmd);
		up(&lock);
		return -EINVAL;
	}

	up(&lock);
	return 0;
}

#ifdef CONFIG_COMPAT
static long dts_eagle_compat_ioctl(struct file *f, unsigned int cmd,
				   unsigned long arg)
{
	unsigned int retCmd = 0;

	switch (cmd) {
	case DTS_EAGLE_COMPAT_IOCTL_GET_CACHE_SIZE: {
		retCmd = DTS_EAGLE_IOCTL_GET_CACHE_SIZE;
		break;
	}

	case DTS_EAGLE_COMPAT_IOCTL_SET_CACHE_SIZE: {
		retCmd = DTS_EAGLE_IOCTL_SET_CACHE_SIZE;
		break;
	}

	case DTS_EAGLE_COMPAT_IOCTL_GET_PARAM: {
		retCmd = DTS_EAGLE_IOCTL_GET_PARAM;
		break;
	}

	case DTS_EAGLE_COMPAT_IOCTL_SET_PARAM: {
		retCmd = DTS_EAGLE_IOCTL_SET_PARAM;
		break;
	}

	case DTS_EAGLE_COMPAT_IOCTL_SET_CACHE_BLOCK: {
		retCmd = DTS_EAGLE_IOCTL_SET_CACHE_BLOCK;
		break;
	}

	case DTS_EAGLE_COMPAT_IOCTL_SET_ACTIVE_DEVICE: {
		retCmd = DTS_EAGLE_IOCTL_SET_ACTIVE_DEVICE;
		break;
	}

	case DTS_EAGLE_COMPAT_IOCTL_GET_LICENSE: {
		retCmd = DTS_EAGLE_IOCTL_GET_LICENSE;
		break;
	}

	case DTS_EAGLE_COMPAT_IOCTL_SET_LICENSE: {
		retCmd = DTS_EAGLE_IOCTL_SET_LICENSE;
		break;
	}

	case DTS_EAGLE_COMPAT_IOCTL_GET_CACHE_PREMIX:{
		retCmd = DTS_EAGLE_IOCTL_GET_CACHE_PREMIX;
		break;
	}

	case DTS_EAGLE_COMPAT_IOCTL_GET_CACHE_POSTMIX: {
		retCmd = DTS_EAGLE_IOCTL_GET_CACHE_POSTMIX;
		break;
	}

	default:
		dts_eagle_drv_err_msg("%s: control 0x%X (invalid control)",
		     __func__, cmd);
		return -EINVAL;
	}

	return dts_eagle_ioctl(f, retCmd, arg);
}
#endif

static const struct file_operations dts_eagle_fops = {
	.owner          =   THIS_MODULE,
	.open           =   dts_eagle_open,
	.release        =   dts_eagle_close,
	.unlocked_ioctl =   dts_eagle_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   =   dts_eagle_compat_ioctl,
#endif
};

static int __init dts_eagle_drv_init(void)
{
	int ret;
	struct device *dev_ret;

	dts_eagle_drv_dbg_msg("%s", __func__);
	printk(KERN_INFO "dts_eagle\n");

	ret = alloc_chrdev_region(&dts_eagle_dev,
		DTS_EAGLE_DRIVER_FIRST_MINOR, DTS_EAGLE_DRIVER_MINOR_CNT,
	     "dts_eagle_ioctl");

	if (ret < 0)
		return ret;

	cdev_init(&dts_eagle_char_dev, &dts_eagle_fops);

	ret = cdev_add(&dts_eagle_char_dev, dts_eagle_dev,
		DTS_EAGLE_DRIVER_MINOR_CNT);

	if (ret < 0)
		return ret;

	ret = IS_ERR(p_dts_eagle_class = class_create(THIS_MODULE, "char"));

	if (ret) {
		cdev_del(&dts_eagle_char_dev);
		unregister_chrdev_region(dts_eagle_dev,
			DTS_EAGLE_DRIVER_MINOR_CNT);

		return PTR_ERR(p_dts_eagle_class);
	}

	ret = IS_ERR(dev_ret = device_create(p_dts_eagle_class, NULL,
		dts_eagle_dev, NULL, "dts_eagle"));

	if (ret) {
		class_destroy(p_dts_eagle_class);
		cdev_del(&dts_eagle_char_dev);
		unregister_chrdev_region(dts_eagle_dev,
			DTS_EAGLE_DRIVER_MINOR_CNT);

		return PTR_ERR(dev_ret);
	}

	if (!_ref_cnt++)
		_init_cb_descs();

	return 0;
}

static void __exit dts_eagle_drv_exit(void)
{
	device_destroy(p_dts_eagle_class, dts_eagle_dev);
	class_destroy(p_dts_eagle_class);
	cdev_del(&dts_eagle_char_dev);
	unregister_chrdev_region(dts_eagle_dev, DTS_EAGLE_DRIVER_MINOR_CNT);

	dts_eagle_drv_dbg_msg("%s", __func__);
	--_ref_cnt;
}

module_init(dts_eagle_drv_init);
module_exit(dts_eagle_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Magesh Devaprakash <magesh.devaprakash@dts.com>");
MODULE_DESCRIPTION("dts eagle drv() char driver");

/* DTS_EAGLE END */
