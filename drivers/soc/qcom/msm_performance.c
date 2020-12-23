/*
 * Copyright (c) 2014-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/moduleparam.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/tick.h>
#include <trace/events/power.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/kthread.h>

/* To handle cpufreq min/max request */
struct cpu_status {
	unsigned int min;
	unsigned int max;
};
static DEFINE_PER_CPU(struct cpu_status, cpu_stats);

struct events {
	spinlock_t cpu_hotplug_lock;
	bool cpu_hotplug;
	bool init_success;
};
static struct events events_group;
static struct task_struct *events_notify_thread;

bool cpu_oc = false;
extern int cpuoc_state;
module_param(cpu_oc, bool, S_IRUSR | S_IWUSR);
bool cpu_minfreq_lock = false;
bool cpu_maxfreq_lock = false;
module_param(cpu_minfreq_lock, bool, S_IRUSR | S_IWUSR);
module_param(cpu_maxfreq_lock, bool, S_IRUSR | S_IWUSR);
/*******************************sysfs start************************************/
static int set_cpu_min_freq(const char *buf, const struct kernel_param *kp)
{
	int i, j, ntokens = 0;
	unsigned int val, cpu;
	const char *cp = buf;
	struct cpu_status *i_cpu_stats;
	struct cpufreq_policy policy;
	cpumask_var_t limit_mask;

	while ((cp = strpbrk(cp + 1, " :")))
		ntokens++;

	if (cpu_minfreq_lock)
		return 0;
	/* CPU:value pair */
	if (!(ntokens % 2))
		return -EINVAL;

	cp = buf;
	cpumask_clear(limit_mask);
	for (i = 0; i < ntokens; i += 2) {
		if (sscanf(cp, "%u:%u", &cpu, &val) != 2)
			return -EINVAL;
		if (cpu > (num_present_cpus() - 1))
			return -EINVAL;
		if (!cpu_oc) {
#if defined(CONFIG_KERNEL_CUSTOM_E7S) || defined(CONFIG_KERNEL_CUSTOM_E7T)
			if (cpuoc_state == 0) {
				if ((cpu >= 0) && (cpu <=3 ) && (val == 1612800))
					val = 633600;
				if ((cpu >= 4) && (cpu <=7 ) && (val == 1804800))
					val = 1113600;
			} else if (cpuoc_state == 1) {
				if ((cpu >= 0) && (cpu <=3 ) && (val == 1843200))
					val = 633600;
				if ((cpu >= 4) && (cpu <=7 ) && (val == 2208000))
					val = 1113600;			
			} else if (cpuoc_state == 2) {
				if ((cpu >= 0) && (cpu <=3 ) && (val == 1843200))
					val = 633600;
				if ((cpu >= 4) && (cpu <=7 ) && (val == 2457600))
					val = 1113600;			
			}
#else
			if (cpuoc_state == 0) {
				if ((cpu >= 0) && (cpu <=3 ) && (val == 1804800))
					val = 633600;
				if ((cpu >= 4) && (cpu <=7 ) && (val == 2208000))
					val = 1113600;
			} else if (cpuoc_state == 1) {
				if ((cpu >= 0) && (cpu <=3 ) && (val == 1804800))
					val = 633600;
				if ((cpu >= 4) && (cpu <=7 ) && (val == 2457600))
					val = 1113600;
			}
#endif
		}

		i_cpu_stats = &per_cpu(cpu_stats, cpu);

		i_cpu_stats->min = val;
		cpumask_set_cpu(cpu, limit_mask);

		cp = strnchr(cp, strlen(cp), ' ');
		cp++;
	}

	/*
	 * Since on synchronous systems policy is shared amongst multiple
	 * CPUs only one CPU needs to be updated for the limit to be
	 * reflected for the entire cluster. We can avoid updating the policy
	 * of other CPUs in the cluster once it is done for at least one CPU
	 * in the cluster
	 */
	get_online_cpus();
	for_each_cpu(i, limit_mask) {
		i_cpu_stats = &per_cpu(cpu_stats, i);

		if (cpufreq_get_policy(&policy, i))
			continue;

		if (cpu_online(i) && (policy.min != i_cpu_stats->min))
			cpufreq_update_policy(i);

		for_each_cpu(j, policy.related_cpus)
			cpumask_clear_cpu(j, limit_mask);
	}
	put_online_cpus();

	return 0;
}

static int get_cpu_min_freq(char *buf, const struct kernel_param *kp)
{
	int cnt = 0, cpu;

	for_each_present_cpu(cpu) {
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt,
				"%d:%u ", cpu, per_cpu(cpu_stats, cpu).min);
	}
	cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "\n");
	return cnt;
}

static const struct kernel_param_ops param_ops_cpu_min_freq = {
	.set = set_cpu_min_freq,
	.get = get_cpu_min_freq,
};
module_param_cb(cpu_min_freq, &param_ops_cpu_min_freq, NULL, 0644);

static int set_cpu_max_freq(const char *buf, const struct kernel_param *kp)
{
	int i, j, ntokens = 0;
	unsigned int val, cpu;
	const char *cp = buf;
	struct cpu_status *i_cpu_stats;
	struct cpufreq_policy policy;
	cpumask_var_t limit_mask;
	int ret;

	while ((cp = strpbrk(cp + 1, " :")))
		ntokens++;

	if (cpu_maxfreq_lock)
		return 0;
	/* CPU:value pair */
	if (!(ntokens % 2))
		return -EINVAL;

	cp = buf;
	cpumask_clear(limit_mask);
	for (i = 0; i < ntokens; i += 2) {
		if (sscanf(cp, "%u:%u", &cpu, &val) != 2)
			return -EINVAL;
		if (cpu > (num_present_cpus() - 1))
			return -EINVAL;
#ifdef CONFIG_ROG_SUPPORT
#if defined(CONFIG_KERNEL_CUSTOM_E7S) || defined(CONFIG_KERNEL_CUSTOM_E7T)
		if ((cpu >= 0) && (cpu <=3)) {
			if ((val == 1612800) || (val == 1747200))
				val = 1843200;
		} else if ((cpu >= 4) && (cpu <=7)) {
			if ((val == 1804800) || (val == 1958400) ||
				(val == 2150400) || (val == 2457600))
				val = 2208000;
		}
#else
		if ((cpu >= 0) && (cpu <=3 ) && (val > 1804800))
			val = 1804800;
		if ((cpu >= 4) && (cpu <=7 ) && (val > 2208000))
			val = 2208000;
#endif
#else
		if (!cpu_oc) {
#if defined(CONFIG_KERNEL_CUSTOM_E7S) || defined(CONFIG_KERNEL_CUSTOM_E7T)
			if (cpuoc_state == 0) {
				if ((cpu >= 0) && (cpu <=3 ) && (val > 1612800))
					val = 1612800;
				if ((cpu >= 4) && (cpu <=7 ) && (val > 1804800))
					val = 1804800;
			} else if (cpuoc_state == 1) {
				if ((cpu >= 0) && (cpu <=3 ) && (val > 1843200))
					val = 1843200;
				if ((cpu >= 4) && (cpu <=7 ) && (val > 2208000))
					val = 2208000;			
			}
#else
			if (cpuoc_state == 0) {
				if ((cpu >= 0) && (cpu <=3 ) && (val > 1804800))
					val = 1804800;
				if ((cpu >= 4) && (cpu <=7 ) && (val > 2208000))
					val = 2208000;
			}
#endif
		}
#endif

		i_cpu_stats = &per_cpu(cpu_stats, cpu);

		i_cpu_stats->max = val;
		cpumask_set_cpu(cpu, limit_mask);

		cp = strnchr(cp, strlen(cp), ' ');
		cp++;
	}

	get_online_cpus();
	for_each_cpu(i, limit_mask) {
		i_cpu_stats = &per_cpu(cpu_stats, i);
		if (cpufreq_get_policy(&policy, i))
			continue;

		if (cpu_online(i) && (policy.max != i_cpu_stats->max)) {
			ret = cpufreq_update_policy(i);
			if (ret)
				continue;
		}
		for_each_cpu(j, policy.related_cpus)
			cpumask_clear_cpu(j, limit_mask);
	}
	put_online_cpus();

	return 0;
}

static int get_cpu_max_freq(char *buf, const struct kernel_param *kp)
{
	int cnt = 0, cpu;
#ifdef CONFIG_ROG_SUPPORT
	int freq;
#endif

	for_each_present_cpu(cpu) {
#ifdef CONFIG_ROG_SUPPORT
		freq = per_cpu(cpu_stats, cpu).max;
#if defined(CONFIG_KERNEL_CUSTOM_E7S) || defined(CONFIG_KERNEL_CUSTOM_E7T)
		if ((cpu >= 0) && (cpu <= 3)) {
			if (freq > 1612800)
				freq = 1612800;
		} else {
			if (freq > 1804800)
				freq = 1804800;
		}
#else
		if ((cpu >= 0) && (cpu <= 3)) {
			if (freq > 1804800)
				freq = 1804800;
		} else {
			if (freq > 2208000)
				freq = 2208000;
		}
#endif
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt,
				"%d:%u ", cpu, freq);
	}
#else
		cnt += snprintf(buf + cnt, PAGE_SIZE - cnt,
				"%d:%u ", cpu, per_cpu(cpu_stats, cpu).max);
	}
#endif
	cnt += snprintf(buf + cnt, PAGE_SIZE - cnt, "\n");
	return cnt;
}

static const struct kernel_param_ops param_ops_cpu_max_freq = {
	.set = set_cpu_max_freq,
	.get = get_cpu_max_freq,
};
module_param_cb(cpu_max_freq, &param_ops_cpu_max_freq, NULL, 0644);

static struct kobject *events_kobj;

static ssize_t show_cpu_hotplug(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "\n");
}
static struct kobj_attribute cpu_hotplug_attr =
__ATTR(cpu_hotplug, 0444, show_cpu_hotplug, NULL);

static struct attribute *events_attrs[] = {
	&cpu_hotplug_attr.attr,
	NULL,
};

static struct attribute_group events_attr_group = {
	.attrs = events_attrs,
};

/*******************************sysfs ends************************************/
static int perf_adjust_notify(struct notifier_block *nb, unsigned long val,
							void *data)
{
	struct cpufreq_policy *policy = data;
	unsigned int cpu = policy->cpu;
	struct cpu_status *cpu_st = &per_cpu(cpu_stats, cpu);
	unsigned int min = cpu_st->min, max = cpu_st->max;


	if (val != CPUFREQ_ADJUST)
		return NOTIFY_OK;

	pr_debug("msm_perf: CPU%u policy before: %u:%u kHz\n", cpu,
						policy->min, policy->max);
	pr_debug("msm_perf: CPU%u seting min:max %u:%u kHz\n", cpu, min, max);

	cpufreq_verify_within_limits(policy, min, max);

	pr_debug("msm_perf: CPU%u policy after: %u:%u kHz\n", cpu,
						policy->min, policy->max);

	return NOTIFY_OK;
}

static struct notifier_block perf_cpufreq_nb = {
	.notifier_call = perf_adjust_notify,
};

static inline void hotplug_notify(int action)
{
	unsigned long flags;

	if (!events_group.init_success)
		return;

	if ((action == CPU_ONLINE) || (action == CPU_DEAD)) {
		spin_lock_irqsave(&(events_group.cpu_hotplug_lock), flags);
		events_group.cpu_hotplug = true;
		spin_unlock_irqrestore(&(events_group.cpu_hotplug_lock), flags);
		wake_up_process(events_notify_thread);
	}
}

static int __ref msm_performance_cpu_callback(struct notifier_block *nfb,
		unsigned long action, void *hcpu)
{

	hotplug_notify(action);

	return NOTIFY_OK;
}

static struct notifier_block __refdata msm_performance_cpu_notifier = {
	.notifier_call = msm_performance_cpu_callback,
};

static int events_notify_userspace(void *data)
{
	unsigned long flags;
	bool notify_change;

	while (1) {

		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock_irqsave(&(events_group.cpu_hotplug_lock), flags);

		if (!events_group.cpu_hotplug) {
			spin_unlock_irqrestore(&(events_group.cpu_hotplug_lock),
									flags);

			schedule();
			if (kthread_should_stop())
				break;
			spin_lock_irqsave(&(events_group.cpu_hotplug_lock),
									flags);
		}

		set_current_state(TASK_RUNNING);
		notify_change = events_group.cpu_hotplug;
		events_group.cpu_hotplug = false;
		spin_unlock_irqrestore(&(events_group.cpu_hotplug_lock), flags);

		if (notify_change)
			sysfs_notify(events_kobj, NULL, "cpu_hotplug");
	}

	return 0;
}

static int init_events_group(void)
{
	int ret;
	struct kobject *module_kobj;

	module_kobj = kset_find_obj(module_kset, KBUILD_MODNAME);
	if (!module_kobj) {
		pr_err("msm_perf: Couldn't find module kobject\n");
		return -ENOENT;
	}

	events_kobj = kobject_create_and_add("events", module_kobj);
	if (!events_kobj) {
		pr_err("msm_perf: Failed to add events_kobj\n");
		return -ENOMEM;
	}

	ret = sysfs_create_group(events_kobj, &events_attr_group);
	if (ret) {
		pr_err("msm_perf: Failed to create sysfs\n");
		return ret;
	}

	spin_lock_init(&(events_group.cpu_hotplug_lock));
	events_notify_thread = kthread_run(events_notify_userspace,
					NULL, "msm_perf:events_notify");
	if (IS_ERR(events_notify_thread))
		return PTR_ERR(events_notify_thread);

	events_group.init_success = true;

	return 0;
}

static int __init msm_performance_init(void)
{
	unsigned int cpu;

	cpufreq_register_notifier(&perf_cpufreq_nb, CPUFREQ_POLICY_NOTIFIER);

	for_each_present_cpu(cpu)
		per_cpu(cpu_stats, cpu).max = UINT_MAX;

	register_cpu_notifier(&msm_performance_cpu_notifier);

	init_events_group();

	return 0;
}
late_initcall(msm_performance_init);


