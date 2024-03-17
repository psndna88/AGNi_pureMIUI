/*
 * Universal Flash Storage Host Initiated Defrag (UFS HID)
 *
 * Copyright (C) 2019-2019 Samsung Electronics Co., Ltd.
 *
 * Authors:
 *	Yongmyung Lee <ymhungry.lee@samsung.com>
 *	Jieon Seol <jieon.seol@samsung.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * See the COPYING file in the top-level directory or visit
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This program is provided "AS IS" and "WITH ALL FAULTS" and
 * without warranty of any kind. You are solely responsible for
 * determining the appropriateness of using and distributing
 * the program and assume all risks associated with your exercise
 * of rights with respect to the program, including but not limited
 * to infringement of third party rights, the risks and costs of
 * program errors, damage to or loss of data, programs or equipment,
 * and unavailability or interruption of operations. Under no
 * circumstances will the contributor of this Program be liable for
 * any damages of any kind arising from your use or distribution of
 * this program.
 *
 * The Linux Foundation chooses to take subject only to the GPLv2
 * license terms, and distributes only under these terms.
 */

#include "ufshcd.h"
#include "ufshid.h"

bool hid_trigger_enable = 1;
static int create_hidfn_enable_proc(void);
static void remove_hidfn_enable_proc(void);

static int ufshid_create_sysfs(struct ufshid_dev *hid);

inline int ufshid_get_state(struct ufsf_feature *ufsf)
{
	return atomic_read(&ufsf->hid_state);
}

inline void ufshid_set_state(struct ufsf_feature *ufsf, int state)
{
	atomic_set(&ufsf->hid_state, state);
}

static inline int ufshid_is_not_present(struct ufshid_dev *hid)
{
	enum UFSHID_STATE cur_state = ufshid_get_state(hid->ufsf);

	if (cur_state != HID_PRESENT) {
		INFO_MSG("hid_state != HID_PRESENT (%d)", cur_state);
		return -ENODEV;
	}
	return 0;
}

static int ufshid_read_attr(struct ufshid_dev *hid, u8 idn, u32 *attr_val)
{
	struct ufs_hba *hba = hid->ufsf->hba;
	int ret = 0;

	pm_runtime_get_sync(hba->dev);

	ret = ufsf_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR, idn, 0,
				    attr_val);
	if (ret) {
		ERR_MSG("read attr [0x%.2X] fail. (%d)", idn, ret);
		goto err_out;
	}

	HID_DEBUG(hid, "hid_attr read [0x%.2X] %u (0x%X)", idn, *attr_val,
		  *attr_val);
	TMSG(hid->ufsf, 0, "[ufshid] read_attr IDN %s (%d)",
	     idn == QUERY_ATTR_IDN_HID_OPERATION ? "HID_OP" :
	     idn == QUERY_ATTR_IDN_HID_FRAG_LEVEL ? "HID_LEV" : "UNKNOWN", idn);
err_out:
	pm_runtime_put_sync(hba->dev);
	return ret;
}

static int ufshid_write_attr(struct ufshid_dev *hid, u8 idn, u32 val)
{
	struct ufs_hba *hba = hid->ufsf->hba;
	int ret = 0;

	pm_runtime_get_sync(hba->dev);

	ret = ufsf_query_attr_retry(hba, UPIU_QUERY_OPCODE_WRITE_ATTR, idn, 0,
				    &val);
	if (ret) {
		ERR_MSG("write attr [0x%.2X] fail. (%d)", idn, ret);
		goto err_out;
	}

	HID_DEBUG(hid, "hid_attr write [0x%.2X] %u (0x%X)", idn, val, val);
	TMSG(hid->ufsf, 0, "[ufshid] write_attr IDN %s (%d)",
	     idn == QUERY_ATTR_IDN_HID_OPERATION ? "HID_OP" :
	     idn == QUERY_ATTR_IDN_HID_FRAG_LEVEL ? "HID_LEV" : "UNKNOWN", idn);
err_out:
	pm_runtime_put_sync(hba->dev);
	return ret;
}

static inline int ufshid_version_check(int spec_version)
{
	INFO_MSG("Support HID Spec : Driver = (%.4x), Device = (%.4x)",
		 UFSHID_VER, spec_version);
	INFO_MSG("HID Driver version (%.6X%s)",
		 UFSHID_DD_VER, UFSHID_DD_VER_POST);

	if (spec_version != UFSHID_VER) {
		ERR_MSG("UFS HID version mismatched");
		return -ENODEV;
	}
	return 0;
}

void ufshid_get_dev_info(struct ufsf_feature *ufsf, u8 *desc_buf)
{
	int ret = 0, spec_version;

	ufsf->hid_dev = NULL;

	if (!(LI_EN_32(&desc_buf[DEVICE_DESC_PARAM_EX_FEAT_SUP]) &
	      UFS_FEATURE_SUPPORT_HID_BIT)) {
		INFO_MSG("bUFSExFeaturesSupport: HID not support");
		goto err_out;
	}

	INFO_MSG("bUFSExFeaturesSupport: HID support");
	spec_version =
		LI_EN_16(&desc_buf[DEVICE_DESC_PARAM_HID_VER]);
	ret = ufshid_version_check(spec_version);
	if (ret)
		goto err_out;

	ufsf->hid_dev = kzalloc(sizeof(struct ufshid_dev), GFP_KERNEL);
	if (!ufsf->hid_dev) {
		ERR_MSG("hid_dev memalloc fail");
		goto err_out;
	}

	ufsf->hid_dev->ufsf = ufsf;
	return;
err_out:
	ufshid_set_state(ufsf, HID_FAILED);
}

static int ufshid_get_analyze_and_issue_execute(struct ufshid_dev *hid)
{
	u32 attr_val;
	int frag_level;

	if (ufshid_write_attr(hid, QUERY_ATTR_IDN_HID_OPERATION,
			      HID_OP_EXECUTE))
		return -EINVAL;
	if (ufshid_read_attr(hid, QUERY_ATTR_IDN_HID_FRAG_LEVEL, &attr_val))
		return -EINVAL;

	frag_level = attr_val & HID_FRAG_LEVEL_MASK;
	HID_DEBUG(hid, "Frag_lv %d Freg_stat %d HID_need_exec %d",
		  frag_level, HID_FRAG_UPDATE_STAT(attr_val),
		  HID_EXECUTE_REQ_STAT(attr_val));

	if (frag_level == HID_LEV_GRAY)
		return -EAGAIN;

	return (HID_EXECUTE_REQ_STAT(attr_val)) ?
		HID_REQUIRED : HID_NOT_REQUIRED;
}

static int ufshid_issue_disable(struct ufshid_dev *hid)
{
	u32 attr_val;

	if (ufshid_write_attr(hid, QUERY_ATTR_IDN_HID_OPERATION,
			      HID_OP_DISABLE))
		return -EINVAL;
	if (ufshid_read_attr(hid, QUERY_ATTR_IDN_HID_FRAG_LEVEL, &attr_val))
		return -EINVAL;

	HID_DEBUG(hid, "Frag_lv %d Freg_stat %d HID_need_exec %d",
		  attr_val & HID_FRAG_LEVEL_MASK,
		  HID_FRAG_UPDATE_STAT(attr_val),
		  HID_EXECUTE_REQ_STAT(attr_val));

	return 0;
}

/*
 * Lock status: hid_sysfs lock was held when called.
 */
static void ufshid_auto_hibern8_enable(struct ufshid_dev *hid,
				       unsigned int val)
{
	struct ufs_hba *hba = hid->ufsf->hba;
	unsigned long flags;
	u32 reg;

	val = !!val;

	/* Update auto hibern8 timer value if supported */
	if (!ufshcd_is_auto_hibern8_supported(hba))
		return;

	pm_runtime_get_sync(hba->dev);
	ufshcd_hold(hba, false);
	down_write(&hba->clk_scaling_lock);
	ufshcd_scsi_block_requests(hba);
	/* wait for all the outstanding requests to finish */
	ufshcd_wait_for_doorbell_clr(hba, U64_MAX);
	spin_lock_irqsave(hba->host->host_lock, flags);

	reg = ufshcd_readl(hba, REG_AUTO_HIBERNATE_IDLE_TIMER);
	INFO_MSG("ahit-reg 0x%X", reg);

	if (val ^ (reg == hba->ahit)) {
		ufshcd_writel(hba, val ? hba->ahit : HID_AUTO_HIBERN8_DISABLE,
			      REG_AUTO_HIBERNATE_IDLE_TIMER);
		/* Make sure the timer gets applied before further operations */
		mb();

		INFO_MSG("[Before] is_auto_enabled %d", hid->is_auto_enabled);
		hid->is_auto_enabled = val;

		reg = ufshcd_readl(hba, REG_AUTO_HIBERNATE_IDLE_TIMER);
		INFO_MSG("[After] is_auto_enabled %d ahit-reg 0x%X",
			 hid->is_auto_enabled, reg);
	} else {
		INFO_MSG("is_auto_enabled %d. so it does not changed",
			 hid->is_auto_enabled);
	}

	spin_unlock_irqrestore(hba->host->host_lock, flags);
	ufshcd_scsi_unblock_requests(hba);
	up_write(&hba->clk_scaling_lock);
	ufshcd_release(hba);
	pm_runtime_put_sync(hba->dev);
}

static void ufshid_block_enter_suspend(struct ufshid_dev *hid)
{
	struct ufs_hba *hba = hid->ufsf->hba;
	unsigned long flags;

#if defined(CONFIG_UFSHID_POC)
	if (unlikely(hid->block_suspend))
		return;

	hid->block_suspend = true;
#endif
	pm_runtime_get_sync(hba->dev);
	ufshcd_hold(hba, false);

	spin_lock_irqsave(hba->host->host_lock, flags);
	HID_DEBUG(hid,
		  "dev->power.usage_count %d hba->clk_gating.active_reqs %d",
		  atomic_read(&hba->dev->power.usage_count),
		  hba->clk_gating.active_reqs);
	spin_unlock_irqrestore(hba->host->host_lock, flags);
}

/*
 * Lock status: hid_sysfs lock was held when called.
 */
static void ufshid_trigger_on(struct ufshid_dev *hid)
{
	if (unlikely(hid->hid_trigger))
		return;

	hid->hid_trigger = true;
	HID_DEBUG(hid, "trigger 0 -> 1");

	ufshid_block_enter_suspend(hid);

	ufshid_auto_hibern8_enable(hid, 0);

	schedule_delayed_work(&hid->hid_trigger_work, 0);
}

static void ufshid_allow_enter_suspend(struct ufshid_dev *hid)
{
	struct ufs_hba *hba = hid->ufsf->hba;
	unsigned long flags;

#if defined(CONFIG_UFSHID_POC)
	if (unlikely(!hid->block_suspend))
		return;

	hid->block_suspend = false;
#endif
	ufshcd_release(hba);
	pm_runtime_mark_last_busy(hba->dev);
	pm_runtime_put_noidle(hba->dev);

	spin_lock_irqsave(hba->host->host_lock, flags);
	HID_DEBUG(hid,
		  "dev->power.usage_count %d hba->clk_gating.active_reqs %d",
		  atomic_read(&hba->dev->power.usage_count),
		  hba->clk_gating.active_reqs);
	spin_unlock_irqrestore(hba->host->host_lock, flags);
}

/*
 * Lock status: hid_sysfs lock was held when called.
 */
static void ufshid_trigger_off(struct ufshid_dev *hid)
{
	if (unlikely(!hid->hid_trigger))
		return;

	hid->hid_trigger = false;
	HID_DEBUG(hid, "hid_trigger 1 -> 0");

	ufshid_issue_disable(hid);

	ufshid_auto_hibern8_enable(hid, 1);

	ufshid_allow_enter_suspend(hid);
}

static void ufshid_trigger_work_fn(struct work_struct *dwork)
{
	struct ufshid_dev *hid;
	int ret;

	hid = container_of(dwork, struct ufshid_dev, hid_trigger_work.work);

	if (ufshid_is_not_present(hid))
		return;

	HID_DEBUG(hid, "start hid_trigger_work_fn");

	ret = ufshid_get_analyze_and_issue_execute(hid);

	mutex_lock(&hid->sysfs_lock);
	if (!hid->hid_trigger) {
		HID_DEBUG(hid, "hid_trigger == false, return");
		mutex_unlock(&hid->sysfs_lock);
		return;
	}

	if (ret == HID_NOT_REQUIRED) {
		ufshid_trigger_off(hid);
		mutex_unlock(&hid->sysfs_lock);
		return;
	} else if (ret == HID_REQUIRED) {
		HID_DEBUG(hid, "HID_REQUIRED, so sched (%d ms)",
			  hid->hid_trigger_delay);
	} else {
		HID_DEBUG(hid, "issue_HID ERR(%X), so resched for retry",
			  ret);
	}
	mutex_unlock(&hid->sysfs_lock);

	schedule_delayed_work(&hid->hid_trigger_work,
			      msecs_to_jiffies(hid->hid_trigger_delay));

	HID_DEBUG(hid, "end hid_trigger_work_fn");
}

void ufshid_init(struct ufsf_feature *ufsf)
{
	struct ufshid_dev *hid;
	int ret;

	INFO_MSG("HID_INIT_START");

	hid = ufsf->hid_dev;
	BUG_ON(!hid);

	hid->hid_trigger = false;
	hid->hid_trigger_delay = HID_TRIGGER_WORKER_DELAY_MS_DEFAULT;
	INIT_DELAYED_WORK(&hid->hid_trigger_work, ufshid_trigger_work_fn);

	hid->hid_debug = false;
#if defined(CONFIG_UFSHID_POC)
	hid->hid_debug = true;
	hid->block_suspend = false;
#endif

	/* If HCI supports auto hibern8, UFS Driver use it default */
	if (ufshcd_is_auto_hibern8_supported(ufsf->hba)){
		hid->is_auto_enabled = true;
		create_hidfn_enable_proc();
	}
	else
		hid->is_auto_enabled = false;

	ret = ufshid_create_sysfs(hid);
	if (ret) {
		ERR_MSG("sysfs init fail. so hid driver disabled");
		kfree(hid);
		ufshid_set_state(ufsf, HID_FAILED);
		return;
	}

	INFO_MSG("UFS HID create sysfs finished");

	ufshid_set_state(ufsf, HID_PRESENT);
}

void ufshid_reset_host(struct ufsf_feature *ufsf)
{
	struct ufshid_dev *hid = ufsf->hid_dev;

	if (!hid)
		return;

	ufshid_set_state(ufsf, HID_RESET);
	cancel_delayed_work_sync(&hid->hid_trigger_work);
}

void ufshid_reset(struct ufsf_feature *ufsf)
{
	struct ufshid_dev *hid = ufsf->hid_dev;

	if (!hid)
		return;

	ufshid_set_state(ufsf, HID_PRESENT);

	/*
	 * hid_trigger will be checked under sysfs_lock in worker.
	 */
	if (hid->hid_trigger)
		schedule_delayed_work(&hid->hid_trigger_work, 0);

	INFO_MSG("reset completed.");
}

static inline void ufshid_remove_sysfs(struct ufshid_dev *hid)
{
	int ret;

	ret = kobject_uevent(&hid->kobj, KOBJ_REMOVE);
	INFO_MSG("kobject removed (%d)", ret);
	kobject_del(&hid->kobj);
}

void ufshid_remove(struct ufsf_feature *ufsf)
{
	struct ufshid_dev *hid = ufsf->hid_dev;

	if (!hid)
		return;

	INFO_MSG("start HID release");

	ufshid_set_state(ufsf, HID_FAILED);

	cancel_delayed_work_sync(&hid->hid_trigger_work);

	mutex_lock(&hid->sysfs_lock);
	ufshid_allow_enter_suspend(hid);
	ufshid_trigger_off(hid);
	ufshid_remove_sysfs(hid);
	remove_hidfn_enable_proc();
	mutex_unlock(&hid->sysfs_lock);

	kfree(hid);

	INFO_MSG("end HID release");
}

/*
 * this function is called in irq context.
 * so cancel_delayed_work_sync() do not use due to waiting.
 */
void ufshid_on_idle(struct ufsf_feature *ufsf)
{
	struct ufshid_dev *hid = ufsf->hid_dev;

	if (!hid)
		return;
	/*
	 * When hid_trigger_work will be scheduled,
	 * check hid_trigger under sysfs_lock.
	 */
	if (!hid->hid_trigger)
		return;

	if (delayed_work_pending(&hid->hid_trigger_work))
		cancel_delayed_work(&hid->hid_trigger_work);

	schedule_delayed_work(&hid->hid_trigger_work, 0);
}

/* sysfs function */
static ssize_t ufshid_sysfs_show_version(struct ufshid_dev *hid, char *buf)
{
	INFO_MSG("HID version (%.4X) D/D version (%.6X%s)",
		 UFSHID_VER, UFSHID_DD_VER, UFSHID_DD_VER_POST);

	return snprintf(buf, PAGE_SIZE,
			"HID version (%.4X) D/D version (%.6X%s)\n",
			UFSHID_VER, UFSHID_DD_VER, UFSHID_DD_VER_POST);
}

static ssize_t ufshid_sysfs_show_trigger(struct ufshid_dev *hid, char *buf)
{
	INFO_MSG("hid_trigger %d", hid->hid_trigger);

	return snprintf(buf, PAGE_SIZE, "%d\n", hid->hid_trigger);
}

static ssize_t ufshid_sysfs_store_trigger(struct ufshid_dev *hid,
					  const char *buf, size_t count)
{
	unsigned long val;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (!hid_trigger_enable){
		INFO_MSG("HID_trigger close!");
		return -EINVAL;
	}

	if (val != 0 && val != 1)
		return -EINVAL;

	INFO_MSG("HID_trigger %lu", val);

	if (val == hid->hid_trigger)
		return count;

	if (val)
		ufshid_trigger_on(hid);
	else
		ufshid_trigger_off(hid);

	return count;
}

static ssize_t ufshid_sysfs_show_trigger_interval(struct ufshid_dev *hid,
						  char *buf)
{
	INFO_MSG("hid_trigger_interval %d", hid->hid_trigger_delay);

	return snprintf(buf, PAGE_SIZE, "%d\n", hid->hid_trigger_delay);
}

static ssize_t ufshid_sysfs_store_trigger_interval(struct ufshid_dev *hid,
						   const char *buf,
						   size_t count)
{
	unsigned long val;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (val < HID_TRIGGER_WORKER_DELAY_MS_MIN ||
	    val > HID_TRIGGER_WORKER_DELAY_MS_MAX) {
		INFO_MSG("hid_trigger_interval (min) %4dms ~ (max) %4dms",
			 HID_TRIGGER_WORKER_DELAY_MS_MIN,
			 HID_TRIGGER_WORKER_DELAY_MS_MAX);
		return -EINVAL;
	}

	hid->hid_trigger_delay = (unsigned int)val;
	INFO_MSG("hid_trigger_interval %d", hid->hid_trigger_delay);

	return count;
}

static ssize_t ufshid_sysfs_show_debug(struct ufshid_dev *hid, char *buf)
{
	INFO_MSG("debug %d", hid->hid_debug);

	return snprintf(buf, PAGE_SIZE, "%d\n", hid->hid_debug);
}

static ssize_t ufshid_sysfs_store_debug(struct ufshid_dev *hid, const char *buf,
					size_t count)
{
	unsigned long val;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (val != 0 && val != 1)
		return -EINVAL;

	hid->hid_debug = val ? true : false;

	INFO_MSG("debug %d", hid->hid_debug);

	return count;
}

static ssize_t ufshid_sysfs_show_color(struct ufshid_dev *hid, char *buf)
{
	u32 attr_val;
	int frag_level;

	if (ufshid_write_attr(hid, QUERY_ATTR_IDN_HID_OPERATION,
			      HID_OP_ANALYZE)) {
		ERR_MSG("query HID_OPERATION fail");
		return -EINVAL;
	}

	if (ufshid_read_attr(hid, QUERY_ATTR_IDN_HID_FRAG_LEVEL, &attr_val)) {
		ERR_MSG("query HID_FRAG_LEVEL fail");
		return -EINVAL;
	}

	frag_level = attr_val & HID_FRAG_LEVEL_MASK;
	INFO_MSG("Frag_lv %d Freg_stat %d HID_need_exec %d", frag_level,
		 HID_FRAG_UPDATE_STAT(attr_val),
		 HID_EXECUTE_REQ_STAT(attr_val));

	return snprintf(buf, PAGE_SIZE, "%s\n",
			frag_level == HID_LEV_RED ? "RED" :
			frag_level == HID_LEV_YELLOW ? "YELLOW" :
			frag_level == HID_LEV_GREEN ? "GREEN" :
			frag_level == HID_LEV_GRAY ? "GRAY" : "UNKNOWN");
}

#if defined(CONFIG_UFSHID_POC)
static ssize_t ufshid_sysfs_show_debug_op(struct ufshid_dev *hid, char *buf)
{
	u32 attr_val;

	if (ufshid_read_attr(hid, QUERY_ATTR_IDN_HID_OPERATION, &attr_val))
		return -EINVAL;

	INFO_MSG("hid_op %d", attr_val);

	return snprintf(buf, PAGE_SIZE, "%d\n", attr_val);
}

static ssize_t ufshid_sysfs_store_debug_op(struct ufshid_dev *hid,
					   const char *buf, size_t count)
{
	unsigned long val;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (val >= HID_OP_MAX)
		return -EINVAL;

	if (hid->hid_trigger) {
		ERR_MSG("debug_op cannot change, current hid_trigger is ON");
		return -EINVAL;
	}

	if (ufshid_write_attr(hid, QUERY_ATTR_IDN_HID_OPERATION, val))
		return -EINVAL;

	INFO_MSG("hid_op %ld is set!", val);
	return count;
}

static ssize_t ufshid_sysfs_show_block_suspend(struct ufshid_dev *hid,
					       char *buf)
{
	INFO_MSG("block suspend %d", hid->block_suspend);

	return snprintf(buf, PAGE_SIZE, "%d\n", hid->block_suspend);
}

static ssize_t ufshid_sysfs_store_block_suspend(struct ufshid_dev *hid,
						const char *buf, size_t count)
{
	unsigned long val;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (val != 0 && val != 1)
		return -EINVAL;

	INFO_MSG("HID_block_suspend %lu", val);

	if (val == hid->block_suspend)
		return count;

	if (val)
		ufshid_block_enter_suspend(hid);
	else
		ufshid_allow_enter_suspend(hid);

	hid->block_suspend = val ? true : false;

	return count;
}

static ssize_t ufshid_sysfs_show_auto_hibern8_enable(struct ufshid_dev *hid,
						     char *buf)
{
	INFO_MSG("HCI auto hibern8 %d", hid->is_auto_enabled);

	return snprintf(buf, PAGE_SIZE, "%d\n", hid->is_auto_enabled);
}

static ssize_t ufshid_sysfs_store_auto_hibern8_enable(struct ufshid_dev *hid,
						      const char *buf,
						      size_t count)
{
	unsigned long val;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (val != 0 && val != 1)
		return -EINVAL;

	ufshid_auto_hibern8_enable(hid, val);

	return count;
}
#endif

/* SYSFS DEFINE */
#define define_sysfs_ro(_name) __ATTR(_name, 0444,			\
				      ufshid_sysfs_show_##_name, NULL)
#define define_sysfs_rw(_name) __ATTR(_name, 0644,			\
				      ufshid_sysfs_show_##_name,	\
				      ufshid_sysfs_store_##_name)

static struct ufshid_sysfs_entry ufshid_sysfs_entries[] = {
	define_sysfs_ro(version),
	define_sysfs_ro(color),

	define_sysfs_rw(trigger),
	define_sysfs_rw(trigger_interval),

	/* debug */
	define_sysfs_rw(debug),
#if defined(CONFIG_UFSHID_POC)
	/* Attribute (RAW) */
	define_sysfs_rw(debug_op),
	define_sysfs_rw(block_suspend),
	define_sysfs_rw(auto_hibern8_enable),
#endif
	__ATTR_NULL
};

static ssize_t ufshid_attr_show(struct kobject *kobj, struct attribute *attr,
				char *page)
{
	struct ufshid_sysfs_entry *entry;
	struct ufshid_dev *hid;
	ssize_t error;

	entry = container_of(attr, struct ufshid_sysfs_entry, attr);
	if (!entry->show)
		return -EIO;

	hid = container_of(kobj, struct ufshid_dev, kobj);
	if (ufshid_is_not_present(hid))
		return -ENODEV;

	mutex_lock(&hid->sysfs_lock);
	error = entry->show(hid, page);
	mutex_unlock(&hid->sysfs_lock);

	return error;
}

static ssize_t ufshid_attr_store(struct kobject *kobj, struct attribute *attr,
				 const char *page, size_t length)
{
	struct ufshid_sysfs_entry *entry;
	struct ufshid_dev *hid;
	ssize_t error;

	entry = container_of(attr, struct ufshid_sysfs_entry, attr);
	if (!entry->store)
		return -EIO;

	hid = container_of(kobj, struct ufshid_dev, kobj);
	if (ufshid_is_not_present(hid))
		return -ENODEV;

	mutex_lock(&hid->sysfs_lock);
	error = entry->store(hid, page, length);
	mutex_unlock(&hid->sysfs_lock);

	return error;
}

static const struct sysfs_ops ufshid_sysfs_ops = {
	.show = ufshid_attr_show,
	.store = ufshid_attr_store,
};

static struct kobj_type ufshid_ktype = {
	.sysfs_ops = &ufshid_sysfs_ops,
	.release = NULL,
};

static int ufshid_create_sysfs(struct ufshid_dev *hid)
{
	struct device *dev = hid->ufsf->hba->dev;
	struct ufshid_sysfs_entry *entry;
	int err;

	hid->sysfs_entries = ufshid_sysfs_entries;

	kobject_init(&hid->kobj, &ufshid_ktype);
	mutex_init(&hid->sysfs_lock);

	INFO_MSG("ufshid creates sysfs ufshid %p dev->kobj %p",
		 &hid->kobj, &dev->kobj);

	err = kobject_add(&hid->kobj, kobject_get(&dev->kobj), "ufshid");
	if (!err) {
		for (entry = hid->sysfs_entries; entry->attr.name != NULL;
		     entry++) {
			INFO_MSG("ufshid sysfs attr creates: %s",
				 entry->attr.name);
			err = sysfs_create_file(&hid->kobj, &entry->attr);
			if (err) {
				ERR_MSG("create entry(%s) failed",
					entry->attr.name);
				goto kobj_del;
			}
		}
		kobject_uevent(&hid->kobj, KOBJ_ADD);
	} else {
		ERR_MSG("kobject_add failed");
	}

	return err;
kobj_del:
	err = kobject_uevent(&hid->kobj, KOBJ_REMOVE);
	INFO_MSG("kobject removed (%d)", err);
	kobject_del(&hid->kobj);
	return -EINVAL;
}

static inline void hidfn_enable_ctrl(struct ufshid_dev *hid, long val)
{
	switch (val) {
	case 0:
		hid_trigger_enable = 0;
		break;
	case 1:
		hid_trigger_enable = 1;
		break;
	default:
		break;
	}

	return;
}

static ssize_t hidfn_enable_write(struct file *filp, const char *ubuf,
				  size_t cnt, loff_t *data)
{
	char buf[64] = {0};
	long val = 0;
	int ret = 0;
	/*int lun = 0;*/

	if (!ufsf_para.ufsf) {
		return -EFAULT;
        }
	if (cnt >= sizeof(buf)) {
		return -EINVAL;
	}
	if (copy_from_user(&buf, ubuf, cnt)) {
		return -EFAULT;
	}
	buf[cnt] = 0;

	ret = kstrtoul(buf, 0, (unsigned long *)&val);
	if (ret < 0)
		return ret;


	if (ufsf_para.ufsf->hid_dev) {
		hidfn_enable_ctrl(ufsf_para.ufsf->hid_dev, val);
	}


	return cnt;
}

static const struct file_operations hidfn_enable_fops = {
	.write = hidfn_enable_write,
};

static int create_hidfn_enable_proc(void)
{
	struct proc_dir_entry *d_entry;

	if (!ufsf_para.ctrl_dir) {
		return -EFAULT;
	}

	d_entry = proc_create("hidfn_enable", S_IWUGO, ufsf_para.ctrl_dir,
			      &hidfn_enable_fops);
	if(!d_entry) {
		return -ENOMEM;
	}

	return 0;
}

static void remove_hidfn_enable_proc(void)
{
	if (ufsf_para.ctrl_dir)
		remove_proc_entry("hidfn_enable", ufsf_para.ctrl_dir);

	return;
}
