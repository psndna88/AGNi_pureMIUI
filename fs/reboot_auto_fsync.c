/*
 * REBOOT AUTO FSYNC v1.0 (psndna88@gmail.com)
 *
 * Automatic file sync on power off, reboot, panic/freeze
 *
 * taken from Dynamic sync control driver v2
 * by andip71 (alias Lord Boeffla)
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

static int dyn_fsync_panic_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	emergency_sync();
	pr_warn("Reboot auto-fsync: panic - force flush!\n");

	return NOTIFY_DONE;
}

static int dyn_fsync_notify_sys(struct notifier_block *this, unsigned long code,
				void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT) 
	{
		emergency_sync();
		pr_warn("Reboot auto-fsync: reboot - force flush!\n");
	}
	return NOTIFY_DONE;
}

static struct notifier_block dyn_fsync_notifier = 
{
	.notifier_call = dyn_fsync_notify_sys,
};

static struct notifier_block dyn_fsync_panic_block = 
{
	.notifier_call  = dyn_fsync_panic_event,
	.priority       = INT_MAX,
};

static void dyn_fsync_init(void)
{
	register_reboot_notifier(&dyn_fsync_notifier);

	atomic_notifier_chain_register(&panic_notifier_list,
		&dyn_fsync_panic_block);

	pr_info("%s Reboot auto-fsync initialisation complete\n", __FUNCTION__);
}

static void dyn_fsync_exit(void)
{
	unregister_reboot_notifier(&dyn_fsync_notifier);

	atomic_notifier_chain_unregister(&panic_notifier_list,
		&dyn_fsync_panic_block);

	pr_info("%s Reboot auto-fsync unregistration complete\n", __FUNCTION__);
}

module_init(dyn_fsync_init);
module_exit(dyn_fsync_exit);

MODULE_AUTHOR("psndna88");
MODULE_DESCRIPTION("REBOOT AUTO FSYNC - Automatic file sync on power off, reboot, panic/freeze");
MODULE_LICENSE("GPL v2");

