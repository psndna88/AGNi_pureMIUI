/*
 * Dynamic sync control driver V2
 * 
 * by andip71 (alias Lord Boeffla)
 * 
 * V 2.2 for sdm845 pappschlumpf (Erik Müller)
 * 
 * All credits for original implemenation to faux123
 * 
 */

#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/writeback.h>
#include <linux/fb.h>
#include <linux/dyn_sync_cntrl.h>

// fsync_mutex protects dyn_fsync_active during suspend / late resume transitions
static DEFINE_MUTEX(fsync_mutex);


// Declarations

bool suspend_active __read_mostly = true;
bool dyn_fsync_active __read_mostly = DYN_FSYNC_ACTIVE_DEFAULT;

struct notifier_block fb_notif;

extern void sync_filesystems(int wait);


// Functions

static ssize_t dyn_fsync_active_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", (dyn_fsync_active ? 1 : 0));
}


static ssize_t dyn_fsync_active_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;

	if(sscanf(buf, "%u\n", &data) == 1)
	{
		if (data == 1) 
		{
			pr_info("%s: dynamic fsync enabled\n", __FUNCTION__);
			dyn_fsync_active = true;
		}
		else if (data == 0) 
		{
			pr_info("%s: dynamic fsync disabled\n", __FUNCTION__);
			dyn_fsync_active = false;
		}
		else
			pr_info("%s: bad value: %u\n", __FUNCTION__, data);
	} 
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);

	return count;
}


static ssize_t dyn_fsync_version_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "version: %u.%u\n",
		DYN_FSYNC_VERSION_MAJOR,
		DYN_FSYNC_VERSION_MINOR);
}


static ssize_t dyn_fsync_suspend_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "suspend active: %u\n", suspend_active);
}


static void dyn_fsync_force_flush(void)
{
	sync_filesystems(0);
	sync_filesystems(1);
}


static int dyn_fsync_panic_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	suspend_active = false;
	dyn_fsync_force_flush();
	pr_warn("dynamic fsync: panic - force flush!\n");

	return NOTIFY_DONE;
}


static int dyn_fsync_notify_sys(struct notifier_block *this, unsigned long code,
				void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT) 
	{
		suspend_active = false;
		dyn_fsync_force_flush();
		pr_warn("dynamic fsync: reboot - force flush!\n");
	}
	return NOTIFY_DONE;
}

static int fb_notifier_cb(struct notifier_block *nb, unsigned long action,
			  void *data)
{
	int *blank = ((struct fb_event *)data)->data;

	/* Parse framebuffer blank events as soon as they occur */
	if (action != FB_EARLY_EVENT_BLANK)
		return NOTIFY_OK;
			
	if (*blank == FB_BLANK_UNBLANK) {
		mutex_lock(&fsync_mutex);
		suspend_active = true;
		mutex_unlock(&fsync_mutex);
	} else {
		mutex_lock(&fsync_mutex);
		suspend_active = false;

		if (dyn_fsync_active) 
		{
			dyn_fsync_force_flush();
		}
		
		mutex_unlock(&fsync_mutex);
	}

	return 0;
}

// Module structures

static struct notifier_block dyn_fsync_notifier = 
{
	.notifier_call = dyn_fsync_notify_sys,
};

static struct kobj_attribute dyn_fsync_active_attribute = 
	__ATTR(Dyn_fsync_active, 0660,
		dyn_fsync_active_show,
		dyn_fsync_active_store);

static struct kobj_attribute dyn_fsync_version_attribute = 
	__ATTR(Dyn_fsync_version, 0444, dyn_fsync_version_show, NULL);

static struct kobj_attribute dyn_fsync_suspend_attribute = 
	__ATTR(Dyn_fsync_suspend, 0444, dyn_fsync_suspend_show, NULL);

static struct attribute *dyn_fsync_active_attrs[] =
{
	&dyn_fsync_active_attribute.attr,
	&dyn_fsync_version_attribute.attr,
	&dyn_fsync_suspend_attribute.attr,
	NULL,
};

static struct attribute_group dyn_fsync_active_attr_group =
{
	.attrs = dyn_fsync_active_attrs,
};

static struct notifier_block dyn_fsync_panic_block = 
{
	.notifier_call  = dyn_fsync_panic_event,
	.priority       = INT_MAX,
};

static struct kobject *dyn_fsync_kobj;


// Module init/exit

static int dyn_fsync_init(void)
{
	int sysfs_result;
	int ret;

	register_reboot_notifier(&dyn_fsync_notifier);
	
	atomic_notifier_chain_register(&panic_notifier_list,
		&dyn_fsync_panic_block);

	dyn_fsync_kobj = kobject_create_and_add("dyn_fsync", kernel_kobj);

	if (!dyn_fsync_kobj) 
	{
		pr_err("%s dyn_fsync_kobj create failed!\n", __FUNCTION__);
		return -ENOMEM;
    }

	sysfs_result = sysfs_create_group(dyn_fsync_kobj,
			&dyn_fsync_active_attr_group);

    if (sysfs_result) 
    {
		pr_err("%s dyn_fsync sysfs create failed!\n", __FUNCTION__);
		kobject_put(dyn_fsync_kobj);
	}

	fb_notif.notifier_call = fb_notifier_cb;
	ret = fb_register_client(&fb_notif);
	if (ret) 
	{
		pr_err("%s: Failed to register msm_drm_notifier callback\n", __func__);

		unregister_reboot_notifier(&dyn_fsync_notifier);

		atomic_notifier_chain_unregister(&panic_notifier_list,
			&dyn_fsync_panic_block);

		if (dyn_fsync_kobj != NULL)
			kobject_put(dyn_fsync_kobj);

		return -EFAULT;
	}

	pr_info("%s dynamic fsync initialisation complete\n", __FUNCTION__);

	return sysfs_result;
}


static void dyn_fsync_exit(void)
{
	unregister_reboot_notifier(&dyn_fsync_notifier);

	atomic_notifier_chain_unregister(&panic_notifier_list,
		&dyn_fsync_panic_block);

	if (dyn_fsync_kobj != NULL)
		kobject_put(dyn_fsync_kobj);
	
	fb_unregister_client(&fb_notif);
		
	pr_info("%s dynamic fsync unregistration complete\n", __FUNCTION__);
}

module_init(dyn_fsync_init);
module_exit(dyn_fsync_exit);

MODULE_AUTHOR("andip71");
MODULE_DESCRIPTION("dynamic fsync - automatic fs sync optimization for msm8974");
MODULE_LICENSE("GPL v2");
