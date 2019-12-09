/*
 * CPUFreq governor based on scheduler-provided CPU utilization data.
 *
 * Copyright (C) 2016, Intel Corporation
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpufreq.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <trace/events/power.h>

#include "sched.h"
#include "tune.h"

#ifdef CONFIG_SCHED_WALT
unsigned long boosted_cpu_util(int cpu);
#endif

/* Stub out fast switch routines present on mainline to reduce the backport
 * overhead. */
#define cpufreq_driver_fast_switch(x, y) 0
#define cpufreq_enable_fast_switch(x)
#define cpufreq_disable_fast_switch(x)

#define UP_RATE_LIMIT				1000
#define DOWN_RATE_LIMIT				1000

/* Frequency cap for target_load1 in KHz */
#define LOAD1_CAP					1132800
/* Frequency cap for target_load2 in KHz */
#define LOAD2_CAP					1440000
#define TARGET_LOAD_1				20
#define TARGET_LOAD_2				50

/* Frequency cap for target_load1 in KHz */
#define LOAD1_CAP_BIGC				1132800
/* Frequency cap for target_load2 in KHz */
#define LOAD2_CAP_BIGC				1900800
#define TARGET_LOAD_1_BIGC 			15
#define TARGET_LOAD_2_BIGC 			50

#define NRGGOV_KTHREAD_PRIORITY		25

struct nrggov_tunables {
	struct gov_attr_set attr_set;
	unsigned int up_rate_limit_us;
	unsigned int down_rate_limit_us;
	unsigned int target_load1;
	unsigned int target_load2;
	unsigned int load1_cap;
	unsigned int load2_cap;
};

struct nrggov_policy {
	struct cpufreq_policy *policy;

	struct nrggov_tunables *tunables;
	struct list_head tunables_hook;

	raw_spinlock_t update_lock;  /* For shared policies */
	u64 last_freq_update_time;
	s64 min_rate_limit_ns;
	s64 up_rate_delay_ns;
	s64 down_rate_delay_ns;
	unsigned int next_freq;

	/* The next fields are only needed if fast switch cannot be used. */
	struct irq_work irq_work;
	struct kthread_work work;
	struct mutex work_lock;
	struct kthread_worker worker;
	struct task_struct *thread;
	bool work_in_progress;

	bool need_freq_update;
};

struct nrggov_cpu {
	struct update_util_data update_util;
	struct nrggov_policy *sg_policy;

	unsigned int cached_raw_freq;
	unsigned long iowait_boost;
	unsigned long iowait_boost_max;
	u64 last_update;

	/* The fields below are only needed when sharing a policy. */
	unsigned long util;
	unsigned long max;
	unsigned int flags;
};

static DEFINE_PER_CPU(struct nrggov_cpu, nrggov_cpu);
static DEFINE_PER_CPU(struct nrggov_tunables, cached_tunables);

/************************ Governor internals ***********************/

static bool nrggov_should_update_freq(struct nrggov_policy *sg_policy, u64 time)
{
	s64 delta_ns;

	if (sg_policy->work_in_progress)
		return false;

	if (unlikely(sg_policy->need_freq_update)) {
		sg_policy->need_freq_update = false;
		/*
		 * This happens when limits change, so forget the previous
		 * next_freq value and force an update.
		 */
		sg_policy->next_freq = UINT_MAX;
		return true;
	}

	delta_ns = time - sg_policy->last_freq_update_time;

	/* No need to recalculate next freq for min_rate_limit_us at least */
	return delta_ns >= sg_policy->min_rate_limit_ns;
}

static bool nrggov_up_down_rate_limit(struct nrggov_policy *sg_policy, u64 time,
				     unsigned int next_freq)
{
	s64 delta_ns;

	delta_ns = time - sg_policy->last_freq_update_time;

	if (next_freq > sg_policy->next_freq &&
	    delta_ns < sg_policy->up_rate_delay_ns)
			return true;

	if (next_freq < sg_policy->next_freq &&
	    delta_ns < sg_policy->down_rate_delay_ns)
			return true;

	return false;
}

static void nrggov_update_commit(struct nrggov_policy *sg_policy, u64 time,
				unsigned int next_freq)
{
	struct cpufreq_policy *policy = sg_policy->policy;

	if (nrggov_up_down_rate_limit(sg_policy, time, next_freq))
		return;

	sg_policy->last_freq_update_time = time;

	if (policy->fast_switch_enabled) {
		if (sg_policy->next_freq == next_freq) {
			trace_cpu_frequency(policy->cur, smp_processor_id());
			return;
		}
		sg_policy->next_freq = next_freq;
		next_freq = cpufreq_driver_fast_switch(policy, next_freq);
		if (next_freq == CPUFREQ_ENTRY_INVALID)
			return;

		policy->cur = next_freq;
		trace_cpu_frequency(next_freq, smp_processor_id());
	} else if (sg_policy->next_freq != next_freq) {
		sg_policy->next_freq = next_freq;
		sg_policy->work_in_progress = true;
		irq_work_queue(&sg_policy->irq_work);
	}
}

/**
 * get_next_freq - Compute a new frequency for a given cpufreq policy.
 * @sg_cpu: energy-dcfc cpu object to compute the new frequency for.
 * @util: Current CPU utilization.
 * @max: CPU capacity.
 *
 * If the utilization is frequency-invariant, choose the new frequency to be
 * proportional to it, that is
 *
 * next_freq = C * max_freq * util / max
 *
 * Otherwise, approximate the would-be frequency-invariant utilization by
 * util_raw * (curr_freq / max_freq) which leads to
 *
 * next_freq = C * curr_freq * util_raw / max
 *
 * Take C = 1.25 for the frequency tipping point at (util / max) = 0.8.
 *
 * The lowest driver-supported frequency which is equal or greater than the raw
 * next_freq (as calculated above) is returned, subject to policy min/max and
 * cpufreq driver limitations.
 */
static unsigned int get_next_freq(struct nrggov_cpu *sg_cpu, unsigned long util,
				  unsigned long max)
{
	struct nrggov_policy *sg_policy = sg_cpu->sg_policy;
	struct cpufreq_policy *policy = sg_policy->policy;
	struct nrggov_tunables *tunables = sg_policy->tunables;
	unsigned int freq;
	unsigned long load = 100 * util / max;

	if(load < tunables->target_load1)
		freq = tunables->load1_cap;
	else if (load >= tunables->target_load1 && load < tunables->target_load2)
		freq = tunables->load2_cap;
	else
		freq = policy->cpuinfo.max_freq;

	/*
	* Little cluster requires small tipping
	* point for fast responsiveness.
	* 
	* Big cluster requires util boost for gaming.
	* We don't use bitshift here because it will cause
	* unecessary frequency jumps way too often.
	* 
	* Big cluster will have 10% higher util than
	* tracked to compensate for certain games
	* having light CPU load.
	* 
	* This should help improve scenarios when games
	* only use CPU load below 50%.
	* 
	* The problem is that when games demand more power
	* and their CPU load is light, the governor will
	* automatically assume it does not require a lot
	* of power and therefore, will choose low frequencies
	* for those tasks, which will cause a lot of stuttering
	* and framerate drops during gameplay.
	*/
	if(policy->cpu < 2)
		freq = (freq + (freq >> 1)) * util / max;
	else 
		freq = freq * (util + ((util * 10) / 100)) / max;

	if (freq == sg_cpu->cached_raw_freq && sg_policy->next_freq != UINT_MAX)
		return sg_policy->next_freq;
	sg_cpu->cached_raw_freq = freq;
	return cpufreq_driver_resolve_freq(policy, freq);
}

static void nrggov_get_util(unsigned long *util, unsigned long *max, u64 time)
{
	int cpu = smp_processor_id();
	struct rq *rq = cpu_rq(cpu);
	unsigned long max_cap, rt;
	s64 delta;

	max_cap = arch_scale_cpu_capacity(NULL, cpu);

	sched_avg_update(rq);
	delta = time - rq->age_stamp;
	if (unlikely(delta < 0))
		delta = 0;
	rt = div64_u64(rq->rt_avg, sched_avg_period() + delta);
	rt = (rt * max_cap) >> SCHED_CAPACITY_SHIFT;

	*util = min(rq->cfs.avg.util_avg + rt, max_cap);
#ifdef CONFIG_SCHED_WALT
	if (!walt_disabled && sysctl_sched_use_walt_cpu_util)
		*util = boosted_cpu_util(cpu);
#endif
	*max = max_cap;
}

static void nrggov_set_iowait_boost(struct nrggov_cpu *sg_cpu, u64 time,
				   unsigned int flags)
{
	if (flags & SCHED_CPUFREQ_IOWAIT) {
		sg_cpu->iowait_boost = sg_cpu->iowait_boost_max;
	} else if (sg_cpu->iowait_boost) {
		s64 delta_ns = time - sg_cpu->last_update;

		/* Clear iowait_boost if the CPU apprears to have been idle. */
		if (delta_ns > TICK_NSEC)
			sg_cpu->iowait_boost = 0;
	}
}

static void nrggov_iowait_boost(struct nrggov_cpu *sg_cpu, unsigned long *util,
			       unsigned long *max)
{
	unsigned long boost_util = sg_cpu->iowait_boost;
	unsigned long boost_max = sg_cpu->iowait_boost_max;

	if (!boost_util)
		return;

	if (*util * boost_max < *max * boost_util) {
		*util = boost_util;
		*max = boost_max;
	}
	sg_cpu->iowait_boost >>= 1;
}

#ifdef CONFIG_CAPACITY_CLAMPING

static inline
void cap_clamp_cpu_range(unsigned int cpu, unsigned int *cap_min,
			 unsigned int *cap_max)
{
	struct cap_clamp_cpu *cgc;

	*cap_min = 0;
	cgc = &cpu_rq(cpu)->cap_clamp_cpu[CAP_CLAMP_MIN];
	if (cgc->node)
		*cap_min = cgc->value;

	*cap_max = SCHED_CAPACITY_SCALE;
	cgc = &cpu_rq(cpu)->cap_clamp_cpu[CAP_CLAMP_MAX];
	if (cgc->node)
		*cap_max = cgc->value;
}

static inline
unsigned int cap_clamp_cpu_util(unsigned int cpu, unsigned int util)
{
	unsigned int cap_max, cap_min;

	cap_clamp_cpu_range(cpu, &cap_min, &cap_max);
	return clamp(util, cap_min, cap_max);
}

static inline
void cap_clamp_compose(unsigned int *cap_min, unsigned int *cap_max,
		       unsigned int j_cap_min, unsigned int j_cap_max)
{
	*cap_min = max(*cap_min, j_cap_min);
	*cap_max = max(*cap_max, j_cap_max);
}

#define cap_clamp_util_range(util, cap_min, cap_max) \
	clamp_t(typeof(util), util, cap_min, cap_max)

#else

#define cap_clamp_cpu_range(cpu, cap_min, cap_max) { }
#define cap_clamp_cpu_util(cpu, util) util
#define cap_clamp_compose(cap_min, cap_max, j_cap_min, j_cap_max) { }
#define cap_clamp_util_range(util, cap_min, cap_max) util

#endif /* CONFIG_CAPACITY_CLAMPING */

static void nrggov_update_single(struct update_util_data *hook, u64 time,
				unsigned int flags)
{
	struct nrggov_cpu *sg_cpu = container_of(hook, struct nrggov_cpu, update_util);
	struct nrggov_policy *sg_policy = sg_cpu->sg_policy;
	struct cpufreq_policy *policy = sg_policy->policy;
	unsigned long util, max;
	unsigned int next_f;

	nrggov_set_iowait_boost(sg_cpu, time, flags);
	sg_cpu->last_update = time;

	if (!nrggov_should_update_freq(sg_policy, time))
		return;

	if (flags & SCHED_CPUFREQ_DL) {
		util = cap_clamp_cpu_util(smp_processor_id(),
					  SCHED_CAPACITY_SCALE);
		next_f = get_next_freq(sg_cpu, util, policy->cpuinfo.max_freq);
	} else {
		nrggov_get_util(&util, &max, time);
		nrggov_iowait_boost(sg_cpu, &util, &max);
		util = cap_clamp_cpu_util(smp_processor_id(), util);
		next_f = get_next_freq(sg_cpu, util, max);
	}
	nrggov_update_commit(sg_policy, time, next_f);
}

static unsigned int nrggov_next_freq_shared(struct nrggov_cpu *sg_cpu,
					   unsigned long util, unsigned long max,
					   unsigned int flags)
{
	struct nrggov_policy *sg_policy = sg_cpu->sg_policy;
	struct cpufreq_policy *policy = sg_policy->policy;
	u64 last_freq_update_time = sg_policy->last_freq_update_time;
	unsigned int j;

	nrggov_iowait_boost(sg_cpu, &util, &max);

	/* Initialize clamping range based on caller CPU constraints */
	cap_clamp_cpu_range(smp_processor_id(), &cap_min, &cap_max);

	for_each_cpu(j, policy->cpus) {
		struct nrggov_cpu *j_sg_cpu;
		unsigned long j_util, j_max;
		s64 delta_ns;

		if (j == smp_processor_id())
			continue;

		j_sg_cpu = &per_cpu(nrggov_cpu, j);
		/*
		 * If the CPU utilization was last updated before the previous
		 * frequency update and the time elapsed between the last update
		 * of the CPU utilization and the last frequency update is long
		 * enough, don't take the CPU into account as it probably is
		 * idle now (and clear iowait_boost for it).
		 */
		delta_ns = last_freq_update_time - j_sg_cpu->last_update;
		if (delta_ns > TICK_NSEC) {
			j_sg_cpu->iowait_boost = 0;
			continue;
		}
		if (j_sg_cpu->flags & SCHED_CPUFREQ_DL)
			j_util = cap_clamp_cpu_util(j, SCHED_CAPACITY_SCALE);
		else
			j_util = j_sg_cpu->util;
		j_max = j_sg_cpu->max;
		if (j_util * max > j_max * util) {
			util = j_util;
			max = j_max;
		}

		nrggov_iowait_boost(j_sg_cpu, &util, &max);

		/*
		 * Update clamping range based on this CPU constraints, but
		 * only if this CPU is not currently idle. Idle CPUs do not
		 * enforce constraints in a shared frequency domain.
		 */
		if (!idle_cpu(j)) {
			cap_clamp_cpu_range(j, &j_cap_min, &j_cap_max);
			cap_clamp_compose(&cap_min, &cap_max,
					  j_cap_min, j_cap_max);
		}
	}

	/* Clamp utilization on aggregated CPUs ranges */
	util = cap_clamp_util_range(util, cap_min, cap_max);
	return get_next_freq(sg_cpu, util, max);
}

static void nrggov_update_shared(struct update_util_data *hook, u64 time,
				unsigned int flags)
{
	struct nrggov_cpu *sg_cpu = container_of(hook, struct nrggov_cpu, update_util);
	struct nrggov_policy *sg_policy = sg_cpu->sg_policy;
	unsigned long util, max;
	unsigned int next_f;

	nrggov_get_util(&util, &max, time);

	raw_spin_lock(&sg_policy->update_lock);

	/* CPU is entering IDLE, reset flags without triggering an update */
	if (flags & SCHED_IDLE) {
		sg_cpu->flags = 0;
		goto done;
	}

	sg_cpu->util = util;
	sg_cpu->max = max;
	sg_cpu->flags = flags;

	nrggov_set_iowait_boost(sg_cpu, time, flags);
	sg_cpu->last_update = time;

	if (nrggov_should_update_freq(sg_policy, time)) {
		next_f = nrggov_next_freq_shared(sg_cpu, util, max, flags);
		nrggov_update_commit(sg_policy, time, next_f);
	}

done:
	raw_spin_unlock(&sg_policy->update_lock);
}

static void nrggov_work(struct kthread_work *work)
{
	struct nrggov_policy *sg_policy = container_of(work, struct nrggov_policy, work);

	mutex_lock(&sg_policy->work_lock);
	__cpufreq_driver_target(sg_policy->policy, sg_policy->next_freq,
				CPUFREQ_RELATION_L);
	mutex_unlock(&sg_policy->work_lock);

	sg_policy->work_in_progress = false;
}

static void nrggov_irq_work(struct irq_work *irq_work)
{
	struct nrggov_policy *sg_policy;

	sg_policy = container_of(irq_work, struct nrggov_policy, irq_work);

	/*
	 * For Real Time and Deadline tasks, energy-dcfc governor shoots the
	 * frequency to maximum. And special care must be taken to ensure that
	 * this kthread doesn't result in that.
	 *
	 * This is (mostly) guaranteed by the work_in_progress flag. The flag is
	 * updated only at the end of the nrggov_work() and before that energy-dcfc
	 * rejects all other frequency scaling requests.
	 *
	 * Though there is a very rare case where the RT thread yields right
	 * after the work_in_progress flag is cleared. The effects of that are
	 * neglected for now.
	 */
	queue_kthread_work(&sg_policy->worker, &sg_policy->work);
}

/************************** sysfs interface ************************/

static struct nrggov_tunables *global_tunables;
static DEFINE_MUTEX(global_tunables_lock);

static inline struct nrggov_tunables *to_nrggov_tunables(struct gov_attr_set *attr_set)
{
	return container_of(attr_set, struct nrggov_tunables, attr_set);
}

static DEFINE_MUTEX(min_rate_lock);

static void update_min_rate_limit_us(struct nrggov_policy *sg_policy)
{
	mutex_lock(&min_rate_lock);
	sg_policy->min_rate_limit_ns = min(sg_policy->up_rate_delay_ns,
					   sg_policy->down_rate_delay_ns);
	mutex_unlock(&min_rate_lock);
}

static ssize_t up_rate_limit_us_show(struct gov_attr_set *attr_set, char *buf)
{
	struct nrggov_tunables *tunables = to_nrggov_tunables(attr_set);

	return sprintf(buf, "%u\n", tunables->up_rate_limit_us);
}

static ssize_t down_rate_limit_us_show(struct gov_attr_set *attr_set, char *buf)
{
	struct nrggov_tunables *tunables = to_nrggov_tunables(attr_set);

	return sprintf(buf, "%u\n", tunables->down_rate_limit_us);
}

static ssize_t target_load1_show(struct gov_attr_set *attr_set, char *buf)
{
	struct nrggov_tunables *tunables = to_nrggov_tunables(attr_set);

	return sprintf(buf, "%u\n", tunables->target_load1);
}

static ssize_t target_load2_show(struct gov_attr_set *attr_set, char *buf)
{
	struct nrggov_tunables *tunables = to_nrggov_tunables(attr_set);

	return sprintf(buf, "%u\n", tunables->target_load2);
}

static ssize_t load1_cap_show(struct gov_attr_set *attr_set, char *buf)
{
	struct nrggov_tunables *tunables = to_nrggov_tunables(attr_set);

	return sprintf(buf, "%u\n", tunables->load1_cap);
}

static ssize_t load2_cap_show(struct gov_attr_set *attr_set, char *buf)
{
	struct nrggov_tunables *tunables = to_nrggov_tunables(attr_set);

	return sprintf(buf, "%u\n", tunables->load2_cap);
}

static ssize_t up_rate_limit_us_store(struct gov_attr_set *attr_set,
				      const char *buf, size_t count)
{
	struct nrggov_tunables *tunables = to_nrggov_tunables(attr_set);
	struct nrggov_policy *sg_policy;
	unsigned int rate_limit_us;

	if (kstrtouint(buf, 10, &rate_limit_us))
		return -EINVAL;

	tunables->up_rate_limit_us = rate_limit_us;

	list_for_each_entry(sg_policy, &attr_set->policy_list, tunables_hook) {
		sg_policy->up_rate_delay_ns = rate_limit_us * NSEC_PER_USEC;
		update_min_rate_limit_us(sg_policy);
	}

	return count;
}

static ssize_t down_rate_limit_us_store(struct gov_attr_set *attr_set,
					const char *buf, size_t count)
{
	struct nrggov_tunables *tunables = to_nrggov_tunables(attr_set);
	struct nrggov_policy *sg_policy;
	unsigned int rate_limit_us;

	if (kstrtouint(buf, 10, &rate_limit_us))
		return -EINVAL;

	tunables->down_rate_limit_us = rate_limit_us;

	list_for_each_entry(sg_policy, &attr_set->policy_list, tunables_hook) {
		sg_policy->down_rate_delay_ns = rate_limit_us * NSEC_PER_USEC;
		update_min_rate_limit_us(sg_policy);
	}

	return count;
}

static ssize_t target_load1_store(struct gov_attr_set *attr_set,
					const char *buf, size_t count)
{
	struct nrggov_tunables *tunables = to_nrggov_tunables(attr_set);
	int value;

	if (kstrtouint(buf, 10, &value))
		return -EINVAL;

	value = min(max(0,value), 100);


	if (value == tunables->target_load1)
		return count;

	tunables->target_load1 = value;

	return count;
}

static ssize_t target_load2_store(struct gov_attr_set *attr_set,
					const char *buf, size_t count)
{
	struct nrggov_tunables *tunables = to_nrggov_tunables(attr_set);
	int value;

	if (kstrtouint(buf, 10, &value))
		return -EINVAL;

	value = min(max(0,value), 100);


	if (value == tunables->target_load2)
		return count;

	tunables->target_load2 = value;

	return count;
}

static ssize_t load1_cap_store(struct gov_attr_set *attr_set,
					const char *buf, size_t count)
{
	struct nrggov_tunables *tunables = to_nrggov_tunables(attr_set);
	unsigned int value;

	if (kstrtouint(buf, 10, &value))
		return -EINVAL;

	if (value == tunables->load1_cap)
		return count;

	tunables->load1_cap = value;

	return count;
}

static ssize_t load2_cap_store(struct gov_attr_set *attr_set,
					const char *buf, size_t count)
{
	struct nrggov_tunables *tunables = to_nrggov_tunables(attr_set);
	unsigned int value;

	if (kstrtouint(buf, 10, &value))
		return -EINVAL;

	if (value == tunables->load2_cap)
		return count;

	tunables->load2_cap = value;

	return count;
}

static struct governor_attr up_rate_limit_us = __ATTR_RW(up_rate_limit_us);
static struct governor_attr down_rate_limit_us = __ATTR_RW(down_rate_limit_us);
static struct governor_attr target_load1 = __ATTR_RW(target_load1);
static struct governor_attr target_load2 = __ATTR_RW(target_load2);
static struct governor_attr load1_cap = __ATTR_RW(load1_cap);
static struct governor_attr load2_cap = __ATTR_RW(load2_cap);

static struct attribute *nrggov_attributes[] = {
	&up_rate_limit_us.attr,
	&down_rate_limit_us.attr,
	&target_load1.attr,
	&target_load2.attr,
	&load1_cap.attr,
	&load2_cap.attr,
	NULL
};

static struct kobj_type nrggov_tunables_ktype = {
	.default_attrs = nrggov_attributes,
	.sysfs_ops = &governor_sysfs_ops,
};

/********************** cpufreq governor interface *********************/
#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_ENERGY
static
#endif
struct cpufreq_governor cpufreq_gov_energy_dcfc;

static struct nrggov_policy *nrggov_policy_alloc(struct cpufreq_policy *policy)
{
	struct nrggov_policy *sg_policy;

	sg_policy = kzalloc(sizeof(*sg_policy), GFP_KERNEL);
	if (!sg_policy)
		return NULL;

	sg_policy->policy = policy;
	init_irq_work(&sg_policy->irq_work, nrggov_irq_work);
	mutex_init(&sg_policy->work_lock);
	raw_spin_lock_init(&sg_policy->update_lock);
	return sg_policy;
}

static void nrggov_policy_free(struct nrggov_policy *sg_policy)
{
	mutex_destroy(&sg_policy->work_lock);
	kfree(sg_policy);
}

static int nrggov_kthread_create(struct nrggov_policy *sg_policy)
{
	struct task_struct *thread;
	struct sched_param param = { .sched_priority = MAX_USER_RT_PRIO / 2 };
	struct cpufreq_policy *policy = sg_policy->policy;
	int ret;

	/* kthread only required for slow path */
	if (policy->fast_switch_enabled)
		return 0;

	init_kthread_work(&sg_policy->work, nrggov_work);
	init_kthread_worker(&sg_policy->worker);
	thread = kthread_create(kthread_worker_fn, &sg_policy->worker,
				"nrggov:%d",
				cpumask_first(policy->related_cpus));
	if (IS_ERR(thread)) {
		pr_err("failed to create nrggov thread: %ld\n", PTR_ERR(thread));
		return PTR_ERR(thread);
	}

	ret = sched_setscheduler_nocheck(thread, SCHED_FIFO, &param);
	if (ret) {
		kthread_stop(thread);
		pr_warn("%s: failed to set SCHED_FIFO\n", __func__);
		return ret;
	}

	sg_policy->thread = thread;
	kthread_bind_mask(thread, policy->related_cpus);
	wake_up_process(thread);

	return 0;
}

static void nrggov_kthread_stop(struct nrggov_policy *sg_policy)
{
	/* kthread only required for slow path */
	if (sg_policy->policy->fast_switch_enabled)
		return;

	flush_kthread_worker(&sg_policy->worker);
	kthread_stop(sg_policy->thread);
}

static struct nrggov_tunables *nrggov_tunables_alloc(struct nrggov_policy *sg_policy)
{
	struct nrggov_tunables *tunables;

	tunables = kzalloc(sizeof(*tunables), GFP_KERNEL);
	if (tunables) {
		gov_attr_set_init(&tunables->attr_set, &sg_policy->tunables_hook);
		if (!have_governor_per_policy())
			global_tunables = tunables;
	}
	return tunables;
}

static void nrggov_tunables_free(struct nrggov_tunables *tunables)
{
	if (!have_governor_per_policy())
		global_tunables = NULL;

	kfree(tunables);
}

static void store_tunables_data(struct nrggov_tunables *tunables,
		struct cpufreq_policy *policy)
{
	struct nrggov_tunables *ptunables;
	unsigned int cpu = cpumask_first(policy->related_cpus);

	ptunables = &per_cpu(cached_tunables, cpu);
	if (!ptunables)
		return;
	ptunables->up_rate_limit_us = tunables->up_rate_limit_us;
	ptunables->down_rate_limit_us = tunables->down_rate_limit_us;
	ptunables->target_load1 = tunables->target_load1;
	ptunables->target_load2 = tunables->target_load2;
	ptunables->load1_cap = tunables->load1_cap;
	ptunables->load2_cap = tunables->load2_cap;

	pr_debug("tunables data saved for cpu[%u]\n", cpu);
}

static void get_tunables_data(struct nrggov_tunables *tunables,
		struct cpufreq_policy *policy)
{
	struct nrggov_tunables *ptunables;
	unsigned int lat;
	unsigned int cpu = cpumask_first(policy->related_cpus);

	ptunables = &per_cpu(cached_tunables, cpu);
	if (!ptunables)
		goto initialize;

	if (ptunables->up_rate_limit_us > 0) {
		tunables->up_rate_limit_us = ptunables->up_rate_limit_us;
		tunables->down_rate_limit_us = ptunables->down_rate_limit_us;
		tunables->target_load1 = ptunables->target_load1;
		tunables->target_load2 = ptunables->target_load2;
		tunables->load1_cap = ptunables->load1_cap;
		tunables->load2_cap = ptunables->load2_cap;
		pr_debug("tunables data restored for cpu[%u]\n", cpu);
		goto out;
	}

initialize:
	if (cpu < 2){
		tunables->target_load1 = TARGET_LOAD_1;
		tunables->target_load2 = TARGET_LOAD_2;
		tunables->load1_cap = LOAD1_CAP;
		tunables->load2_cap = LOAD2_CAP;
	} else {
		tunables->target_load1 = TARGET_LOAD_1_BIGC;
		tunables->target_load2 = TARGET_LOAD_2_BIGC;
		tunables->load1_cap = LOAD1_CAP_BIGC;
		tunables->load2_cap = LOAD2_CAP_BIGC;
	}

	tunables->up_rate_limit_us = UP_RATE_LIMIT;
	tunables->down_rate_limit_us = DOWN_RATE_LIMIT;

	lat = policy->cpuinfo.transition_latency / NSEC_PER_USEC;
	if (lat) {
		tunables->up_rate_limit_us *= lat;
		tunables->down_rate_limit_us *= lat;
	}

	pr_debug("tunables data initialized for cpu[%u]\n", cpu);
out:
	return;
}

static int nrggov_init(struct cpufreq_policy *policy)
{
	struct nrggov_policy *sg_policy;
	struct nrggov_tunables *tunables;
	int ret = 0;

	/* State should be equivalent to EXIT */
	if (policy->governor_data)
		return -EBUSY;

	sg_policy = nrggov_policy_alloc(policy);
	if (!sg_policy)
		return -ENOMEM;

	ret = nrggov_kthread_create(sg_policy);
	if (ret)
		goto free_sg_policy;

	mutex_lock(&global_tunables_lock);

	if (global_tunables) {
		if (WARN_ON(have_governor_per_policy())) {
			ret = -EINVAL;
			goto stop_kthread;
		}
		policy->governor_data = sg_policy;
		sg_policy->tunables = global_tunables;

		gov_attr_set_get(&global_tunables->attr_set, &sg_policy->tunables_hook);
		goto out;
	}

	tunables = nrggov_tunables_alloc(sg_policy);
	if (!tunables) {
		ret = -ENOMEM;
		goto stop_kthread;
	}

	get_tunables_data(tunables, policy);

	policy->governor_data = sg_policy;
	sg_policy->tunables = tunables;

	ret = kobject_init_and_add(&tunables->attr_set.kobj, &nrggov_tunables_ktype,
				   get_governor_parent_kobj(policy), "%s",
				   cpufreq_gov_energy_dcfc.name);
	if (ret)
		goto fail;

 out:
	mutex_unlock(&global_tunables_lock);

	cpufreq_enable_fast_switch(policy);
	return 0;

 fail:
	policy->governor_data = NULL;
	nrggov_tunables_free(tunables);

stop_kthread:
	nrggov_kthread_stop(sg_policy);

free_sg_policy:
	mutex_unlock(&global_tunables_lock);

	nrggov_policy_free(sg_policy);
	pr_err("initialization failed (error %d)\n", ret);
	return ret;
}

static int nrggov_exit(struct cpufreq_policy *policy)
{
	struct nrggov_policy *sg_policy = policy->governor_data;
	struct nrggov_tunables *tunables = sg_policy->tunables;
	unsigned int count;

	cpufreq_disable_fast_switch(policy);

	mutex_lock(&global_tunables_lock);

	store_tunables_data(sg_policy->tunables, policy);
	count = gov_attr_set_put(&tunables->attr_set, &sg_policy->tunables_hook);
	policy->governor_data = NULL;
	if (!count)
		nrggov_tunables_free(tunables);

	mutex_unlock(&global_tunables_lock);

	nrggov_kthread_stop(sg_policy);
	nrggov_policy_free(sg_policy);

	return 0;
}

static int nrggov_start(struct cpufreq_policy *policy)
{
	struct nrggov_policy *sg_policy = policy->governor_data;
	unsigned int cpu;

	sg_policy->up_rate_delay_ns =
		sg_policy->tunables->up_rate_limit_us * NSEC_PER_USEC;
	sg_policy->down_rate_delay_ns =
		sg_policy->tunables->down_rate_limit_us * NSEC_PER_USEC;
	update_min_rate_limit_us(sg_policy);
	sg_policy->last_freq_update_time = 0;
	sg_policy->next_freq = UINT_MAX;
	sg_policy->work_in_progress = false;
	sg_policy->need_freq_update = false;

	for_each_cpu(cpu, policy->cpus) {
		struct nrggov_cpu *sg_cpu = &per_cpu(nrggov_cpu, cpu);

		sg_cpu->sg_policy = sg_policy;
		if (policy_is_shared(policy)) {
			sg_cpu->util = 0;
			sg_cpu->max = 0;
			sg_cpu->flags = SCHED_CPUFREQ_DL;
			sg_cpu->last_update = 0;
			sg_cpu->cached_raw_freq = 0;
			sg_cpu->iowait_boost = 0;
			sg_cpu->iowait_boost_max = policy->cpuinfo.max_freq;
			cpufreq_add_update_util_hook(cpu, &sg_cpu->update_util,
						     nrggov_update_shared);
		} else {
			cpufreq_add_update_util_hook(cpu, &sg_cpu->update_util,
						     nrggov_update_single);
		}
	}
	return 0;
}

static int nrggov_stop(struct cpufreq_policy *policy)
{
	struct nrggov_policy *sg_policy = policy->governor_data;
	unsigned int cpu;

	for_each_cpu(cpu, policy->cpus)
		cpufreq_remove_update_util_hook(cpu);

	synchronize_sched();

	irq_work_sync(&sg_policy->irq_work);
	kthread_cancel_work_sync(&sg_policy->work);

	return 0;
}

static int nrggov_limits(struct cpufreq_policy *policy)
{
	struct nrggov_policy *sg_policy = policy->governor_data;

	if (!policy->fast_switch_enabled) {
		mutex_lock(&sg_policy->work_lock);
		cpufreq_policy_apply_limits(policy);
		mutex_unlock(&sg_policy->work_lock);
	}

	sg_policy->need_freq_update = true;

	return 0;
}

static int cpufreq_energy_dcfc_cb(struct cpufreq_policy *policy,
				unsigned int event)
{
	switch(event) {
	case CPUFREQ_GOV_POLICY_INIT:
		return nrggov_init(policy);
	case CPUFREQ_GOV_POLICY_EXIT:
		return nrggov_exit(policy);
	case CPUFREQ_GOV_START:
		return nrggov_start(policy);
	case CPUFREQ_GOV_STOP:
		return nrggov_stop(policy);
	case CPUFREQ_GOV_LIMITS:
		return nrggov_limits(policy);
	default:
		BUG();
	}
}

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_ENERGY
static
#endif
struct cpufreq_governor cpufreq_gov_energy_dcfc = {
	.name = "energy-dcfc",
	.governor = cpufreq_energy_dcfc_cb,
	.owner = THIS_MODULE,
};

static int __init nrggov_register(void)
{
	return cpufreq_register_governor(&cpufreq_gov_energy_dcfc);
}
fs_initcall(nrggov_register);
