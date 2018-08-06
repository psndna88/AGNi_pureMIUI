/*
 * CPUFreq governor based on scheduler-provided CPU utilization data.
 *
 * Copyright (C) 2016, Intel Corporation
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * Copyright (C) 2017, Joe Maples <joe@frap129.org>
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
#include <linux/state_notifier.h>
#include "sched.h"
#include "tune.h"

unsigned long boosted_cpu_util(int cpu);

/* Stub out fast switch routines present on mainline to reduce the backport
 * overhead. */
#define cpufreq_driver_fast_switch(x, y) 0
#define cpufreq_enable_fast_switch(x)
#define cpufreq_disable_fast_switch(x)
#define LATENCY_MULTIPLIER			(2000)
#define dugov_KTHREAD_PRIORITY	50
#define DEFAULT_SUSPEND_MAX_FREQ_SILVER	1113600
#define DEFAULT_SUSPEND_MAX_FREQ_GOLD	300000
#define DEFAULT_SUSPEND_CAPACITY_FACTOR 10

#define UP_RATE_LIMIT				2000
#define DOWN_RATE_LIMIT				5000
#define BIT_SHIFT_1 				9
#define BIT_SHIFT_2 				9
#define TARGET_LOAD_1				32
#define TARGET_LOAD_2				73

#define UP_RATE_LIMIT_BIGC			2000
#define DOWN_RATE_LIMIT_BIGC			5000
#define BIT_SHIFT_1_BIGC 			10
#define BIT_SHIFT_2_BIGC 			6
#define TARGET_LOAD_1_BIGC 			24
#define TARGET_LOAD_2_BIGC 			71

struct dugov_tunables {
	struct gov_attr_set attr_set;
	unsigned int up_rate_limit_us;
	unsigned int down_rate_limit_us;
	bool iowait_boost_enable;
	unsigned int silver_suspend_max_freq;
	unsigned int gold_suspend_max_freq;
	unsigned int suspend_capacity_factor;
	unsigned int bit_shift1;
	unsigned int bit_shift2;
	unsigned int target_load1;
	unsigned int target_load2;
};

struct dugov_policy {
	struct cpufreq_policy *policy;

	struct dugov_tunables *tunables;
	struct list_head tunables_hook;

	raw_spinlock_t update_lock;  /* For shared policies */
	u64 last_freq_update_time;
	s64 min_rate_limit_ns;
	s64 up_rate_delay_ns;
	s64 down_rate_delay_ns;
	unsigned int next_freq;
	unsigned int cached_raw_freq;

	/* The next fields are only needed if fast switch cannot be used. */
	struct irq_work irq_work;
	struct kthread_work work;
	struct mutex work_lock;
	struct kthread_worker worker;
	struct task_struct *thread;
	bool work_in_progress;
	bool need_freq_update;
};

struct dugov_cpu {
	struct update_util_data update_util;
	struct dugov_policy *du_policy;

	bool iowait_boost_pending;
	unsigned int iowait_boost;
	unsigned int iowait_boost_max;
	u64 last_update;

	/* The fields below are only needed when sharing a policy. */
	unsigned long util;
	unsigned long max;
	unsigned int flags;
	unsigned int cpu;

	/* The field below is for single-CPU policies only. */
#ifdef CONFIG_NO_HZ_COMMON
	unsigned long saved_idle_calls;
#endif
};

static DEFINE_PER_CPU(struct dugov_cpu, dugov_cpu);
static unsigned int stale_ns;

/************************ Governor internals ***********************/

static bool dugov_should_update_freq(struct dugov_policy *du_policy, u64 time)
{
	s64 delta_ns;

	if (unlikely(du_policy->need_freq_update)) {
		return true;
	}

	delta_ns = time - du_policy->last_freq_update_time;

	/* No need to recalculate next freq for min_rate_limit_us at least */
	return delta_ns >= du_policy->min_rate_limit_ns;
}

static bool dugov_up_down_rate_limit(struct dugov_policy *du_policy, u64 time,
				     unsigned int next_freq)
{
	s64 delta_ns;

	delta_ns = time - du_policy->last_freq_update_time;

	if (next_freq > du_policy->next_freq &&
	    delta_ns < du_policy->up_rate_delay_ns)
			return true;

	if (next_freq < du_policy->next_freq &&
	    delta_ns < du_policy->down_rate_delay_ns)
			return true;

	return false;
}

static void dugov_update_commit(struct dugov_policy *du_policy, u64 time,
				unsigned int next_freq)
{
	struct cpufreq_policy *policy = du_policy->policy;

	if (dugov_up_down_rate_limit(du_policy, time, next_freq))
		return;

	du_policy->last_freq_update_time = time;

	if (policy->fast_switch_enabled) {
		if (du_policy->next_freq == next_freq) {
			//trace_cpu_frequency(policy->cur, smp_processor_id());
			return;
		}
		du_policy->next_freq = next_freq;
		next_freq = cpufreq_driver_fast_switch(policy, next_freq);
		if (!next_freq)
			return;

		policy->cur = next_freq;
		//trace_cpu_frequency(next_freq, smp_processor_id());
	} else if (du_policy->next_freq != next_freq || !du_policy->work_in_progress) {
		du_policy->next_freq = next_freq;
		du_policy->work_in_progress = true;
		irq_work_queue(&du_policy->irq_work);
	}
}

/**
 * get_next_freq - Compute a new frequency for a given cpufreq policy.
 * @du_policy: darkutil policy object to compute the new frequency for.
 * @util: Current CPU utilization.
 * @max: CPU capacity.
 *
 * When the device is awake, the following is true:
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
 * When the device is suspended, the following is true:
 * CPU Capacity is increased such that
 *
 * max = max * capacity_factor / (capacity_factor - 1)
 *
 * This way, the divisor for frequency calculation is 1/capcity_factor larger,
 * resulting into a lower calculated frequency.
 *
 * If the resulting frequency is more than the cluster's respective maximum in
 * suspend, i.e {gold/silver}_suspend_max_freq, then the max is set instead.
 *
 * If the respective max freqeuency in suspend is 0, the calculated frequency is
 * always honored.
 *
 * The lowest driver-supported frequency which is equal or greater than the raw
 * next_freq (as calculated above) is returned, subject to policy min/max and
 * cpufreq driver limitations.
 */
static unsigned int get_next_freq(struct dugov_policy *du_policy,
				  unsigned long util, unsigned long max)
{
	struct cpufreq_policy *policy = du_policy->policy;
	struct dugov_tunables *tunables = du_policy->tunables;
	unsigned long load = 100 * util / max;
	unsigned int freq = arch_scale_freq_invariant() ?
				policy->cpuinfo.max_freq : policy->cur;
	unsigned int capacity_factor, silver_max_freq, gold_max_freq;

	if(state_suspended) {
		capacity_factor = du_policy->tunables->suspend_capacity_factor;
		silver_max_freq = du_policy->tunables->silver_suspend_max_freq;
		gold_max_freq = du_policy->tunables->gold_suspend_max_freq;
		max = max * (capacity_factor + 1) / capacity_factor;
	}

	if(load < tunables->target_load1){
		freq = (freq + (freq >> tunables->bit_shift1)) * util / max;
	} else if (load >= tunables->target_load1 && load < tunables->target_load2){
		freq = (freq + (freq >> 2)) * util / max;
	} else {
		freq = (freq - (freq >> tunables->bit_shift2)) * util / max;
	}

	switch(policy->cpu){
	case 0:
	case 1:
	case 2:
	case 3:
		if(state_suspended &&  silver_max_freq > 0 && silver_max_freq < freq)
			return silver_max_freq;
		break;
	case 4:
	case 5:
		if(state_suspended && gold_max_freq > 0 && gold_max_freq < freq)
			return gold_max_freq;
		break;
	case 6:
	case 7:
		if(state_suspended)
			return policy->min;
		break;
	default:
		BUG();
	}

	if (freq == du_policy->cached_raw_freq && !du_policy->need_freq_update)
		return du_policy->next_freq;
	du_policy->need_freq_update = false;
	du_policy->cached_raw_freq = freq;
	return cpufreq_driver_resolve_freq(policy, freq);
}

static inline bool use_pelt(void)
{
#ifdef CONFIG_SCHED_WALT
	return (!sysctl_sched_use_walt_cpu_util || walt_disabled);
#else
	return true;
#endif
}

static void dugov_get_util(unsigned long *util, unsigned long *max, u64 time)
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

	if (!walt_disabled && sysctl_sched_use_walt_cpu_util)
		*util = boosted_cpu_util(cpu);

	*max = max_cap;
}

static void dugov_set_iowait_boost(struct dugov_cpu *du_cpu, u64 time,
				   unsigned int flags)
{
	struct dugov_policy *du_policy = du_cpu->du_policy;

	if (!du_policy->tunables->iowait_boost_enable)
		return;

	if (flags & SCHED_CPUFREQ_IOWAIT) {
		if (du_cpu->iowait_boost_pending)
			return;

		du_cpu->iowait_boost_pending = true;

		if (du_cpu->iowait_boost) {
			du_cpu->iowait_boost <<= 1;
			if (du_cpu->iowait_boost > du_cpu->iowait_boost_max)
				du_cpu->iowait_boost = du_cpu->iowait_boost_max;
		} else {
			du_cpu->iowait_boost = du_cpu->du_policy->policy->min;
		}
	} else if (du_cpu->iowait_boost) {
		s64 delta_ns = time - du_cpu->last_update;

		/* Clear iowait_boost if the CPU apprears to have been idle. */
		if (delta_ns > TICK_NSEC) {
			du_cpu->iowait_boost = 0;
			du_cpu->iowait_boost_pending = false;
		}
	}
}

static void dugov_iowait_boost(struct dugov_cpu *du_cpu, unsigned long *util,
			       unsigned long *max)
{
	unsigned int boost_util, boost_max;

	if (!du_cpu->iowait_boost)
		return;

	if (du_cpu->iowait_boost_pending) {
		du_cpu->iowait_boost_pending = false;
	} else {
		du_cpu->iowait_boost >>= 1;
		if (du_cpu->iowait_boost < du_cpu->du_policy->policy->min) {
			du_cpu->iowait_boost = 0;
			return;
		}
	}

	boost_util = du_cpu->iowait_boost;
	boost_max = du_cpu->iowait_boost_max;

	if (*util * boost_max < *max * boost_util) {
		*util = boost_util;
		*max = boost_max;
	}
}

#ifdef CONFIG_NO_HZ_COMMON
static bool dugov_cpu_is_busy(struct dugov_cpu *du_cpu)
{
	unsigned long idle_calls = tick_nohz_get_idle_calls();
	bool ret = idle_calls == du_cpu->saved_idle_calls;

	du_cpu->saved_idle_calls = idle_calls;
	return ret;
}
#else
static inline bool dugov_cpu_is_busy(struct dugov_cpu *du_cpu) { return false; }
#endif /* CONFIG_NO_HZ_COMMON */

static void dugov_update_single(struct update_util_data *hook, u64 time,
				unsigned int flags)
{
	struct dugov_cpu *du_cpu = container_of(hook, struct dugov_cpu, update_util);
	struct dugov_policy *du_policy = du_cpu->du_policy;
	struct cpufreq_policy *policy = du_policy->policy;
	unsigned long util, max;
	unsigned int next_f;
	bool busy;

	dugov_set_iowait_boost(du_cpu, time, flags);
	du_cpu->last_update = time;

	/*
	 * For slow-switch systems, single policy requests can't run at the
	 * moment if update is in progress, unless we acquire update_lock.
	 */
	if (du_policy->work_in_progress)
		return;

	if (!dugov_should_update_freq(du_policy, time))
		return;

	busy = use_pelt() && dugov_cpu_is_busy(du_cpu);

	if (flags & SCHED_CPUFREQ_DL) {
		/* clear cache when it's bypassed */
		du_policy->cached_raw_freq = 0;
		next_f = get_next_freq(du_policy, util, policy->cpuinfo.max_freq);
	} else {
		dugov_get_util(&util, &max, time);
		dugov_iowait_boost(du_cpu, &util, &max);
		next_f = get_next_freq(du_policy, util, max);
		/*
		 * Do not reduce the frequency if the CPU has not been idle
		 * recently, as the reduction is likely to be premature then.
		 */
		if (busy && next_f < du_policy->next_freq) {
			next_f = du_policy->next_freq;
		du_policy->cached_raw_freq = 0;
	}
    }
	dugov_update_commit(du_policy, time, next_f);
}

static unsigned int dugov_next_freq_shared(struct dugov_cpu *du_cpu,
					   unsigned long util, unsigned long max,
					   unsigned int flags)
{
	struct dugov_policy *du_policy = du_cpu->du_policy;
	struct cpufreq_policy *policy = du_policy->policy;
	u64 last_freq_update_time = du_policy->last_freq_update_time;
	unsigned int j;

	for_each_cpu(j, policy->cpus) {
		struct dugov_cpu *j_du_cpu = &per_cpu(dugov_cpu, j);
		unsigned long j_util, j_max;
		s64 delta_ns;

		/*
		 * If the CPU utilization was last updated before the previous
		 * frequency update and the time elapsed between the last update
		 * of the CPU utilization and the last frequency update is long
		 * enough, don't take the CPU into account as it probably is
		 * idle now (and clear iowait_boost for it).
		 */
		delta_ns = last_freq_update_time - j_du_cpu->last_update;
		if (delta_ns > stale_ns) {
			j_du_cpu->iowait_boost = 0;
			j_du_cpu->iowait_boost_pending = false;
			continue;
		}
		if (j_du_cpu->flags & SCHED_CPUFREQ_DL){
			/* clear cache when it's bypassed */
			du_policy->cached_raw_freq = 0;
			return policy->cpuinfo.max_freq;
		}
		
		j_util = j_du_cpu->util;
		j_max = j_du_cpu->max;
		if (j_util * max >= j_max * util) {
			util = j_util;
			max = j_max;
		}

		dugov_iowait_boost(j_du_cpu, &util, &max);
	}

	return get_next_freq(du_policy, util, max);
}

static void dugov_update_shared(struct update_util_data *hook, u64 time,
				unsigned int flags)
{
	struct dugov_cpu *du_cpu = container_of(hook, struct dugov_cpu, update_util);
	struct dugov_policy *du_policy = du_cpu->du_policy;
	unsigned long util, max;
	unsigned int next_f;

	dugov_get_util(&util, &max, time);

	raw_spin_lock(&du_policy->update_lock);
	
	if (flags & SCHED_IDLE) {
		du_cpu->flags = 0;
		goto done;
	}

	du_cpu->util = util;
	du_cpu->max = max;
	du_cpu->flags = flags;

	dugov_set_iowait_boost(du_cpu, time, flags);
	du_cpu->last_update = time;

	if (dugov_should_update_freq(du_policy, time)) {
		if (flags & SCHED_CPUFREQ_DL){
			/* clear cache when it's bypassed */
			du_policy->cached_raw_freq = 0;
			next_f = du_policy->policy->cpuinfo.max_freq;
		}else{
			next_f = dugov_next_freq_shared(du_cpu, util, max, flags);
		}
		dugov_update_commit(du_policy, time, next_f);
	}
done:
	raw_spin_unlock(&du_policy->update_lock);
}

static void dugov_work(struct kthread_work *work)
{
	struct dugov_policy *du_policy = container_of(work, struct dugov_policy, work);
	unsigned int freq;
	unsigned long flags;

	/*
	 * Hold du_policy->update_lock shortly to handle the case where:
	 * incase du_policy->next_freq is read here, and then updated by
	 * sugov_update_shared just before work_in_progress is set to false
	 * here, we may miss queueing the new update.
	 *
	 * Note: If a work was queued after the update_lock is released,
	 * sugov_work will just be called again by kthread_work code; and the
	 * request will be proceed before the sugov thread sleeps.
	 */
	raw_spin_lock_irqsave(&du_policy->update_lock, flags);
	freq = du_policy->next_freq;
	du_policy->work_in_progress = false;
	raw_spin_unlock_irqrestore(&du_policy->update_lock, flags);

	mutex_lock(&du_policy->work_lock);
	__cpufreq_driver_target(du_policy->policy, freq, CPUFREQ_RELATION_L);

	mutex_unlock(&du_policy->work_lock);

}

static void dugov_irq_work(struct irq_work *irq_work)
{
	struct dugov_policy *du_policy;

	du_policy = container_of(irq_work, struct dugov_policy, irq_work);

	/*
	 * For RT and deadline tasks, the darkutil governor shoots the
	 * frequency to maximum. Special care must be taken to ensure that this
	 * kthread doesn't result in the same behavior.
	 *
	 * This is (mostly) guaranteed by the work_in_progress flag. The flag is
	 * updated only at the end of the dugov_work() function and before that
	 * the darkutil governor rejects all other frequency scaling requests.
	 *
	 * There is a very rare case though, where the RT thread yields right
	 * after the work_in_progress flag is cleared. The effects of that are
	 * neglected for now.
	 */
	queue_kthread_work(&du_policy->worker, &du_policy->work);
}

/************************** sysfs interface ************************/

static struct dugov_tunables *global_tunables;
static DEFINE_MUTEX(global_tunables_lock);

static inline struct dugov_tunables *to_dugov_tunables(struct gov_attr_set *attr_set)
{
	return container_of(attr_set, struct dugov_tunables, attr_set);
}

static DEFINE_MUTEX(min_rate_lock);

static void update_min_rate_limit_us(struct dugov_policy *du_policy)
{
	mutex_lock(&min_rate_lock);
	du_policy->min_rate_limit_ns = min(du_policy->up_rate_delay_ns,
					   du_policy->down_rate_delay_ns);
	mutex_unlock(&min_rate_lock);
}

static ssize_t up_rate_limit_us_show(struct gov_attr_set *attr_set, char *buf)
{
	struct dugov_tunables *tunables = to_dugov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->up_rate_limit_us);
}

static ssize_t down_rate_limit_us_show(struct gov_attr_set *attr_set, char *buf)
{
	struct dugov_tunables *tunables = to_dugov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->down_rate_limit_us);
}

static ssize_t up_rate_limit_us_store(struct gov_attr_set *attr_set,
				      const char *buf, size_t count)
{
	struct dugov_tunables *tunables = to_dugov_tunables(attr_set);
	struct dugov_policy *du_policy;
	unsigned int rate_limit_us;

	if (kstrtouint(buf, 10, &rate_limit_us))
		return -EINVAL;

	tunables->up_rate_limit_us = rate_limit_us;

	list_for_each_entry(du_policy, &attr_set->policy_list, tunables_hook) {
		du_policy->up_rate_delay_ns = rate_limit_us * NSEC_PER_USEC;
		update_min_rate_limit_us(du_policy);
	}

	return count;
}

static ssize_t down_rate_limit_us_store(struct gov_attr_set *attr_set,
					const char *buf, size_t count)
{
	struct dugov_tunables *tunables = to_dugov_tunables(attr_set);
	struct dugov_policy *du_policy;
	unsigned int rate_limit_us;

	if (kstrtouint(buf, 10, &rate_limit_us))
		return -EINVAL;

	tunables->down_rate_limit_us = rate_limit_us;

	list_for_each_entry(du_policy, &attr_set->policy_list, tunables_hook) {
		du_policy->down_rate_delay_ns = rate_limit_us * NSEC_PER_USEC;
		update_min_rate_limit_us(du_policy);
	}

	return count;
}

static ssize_t iowait_boost_enable_show(struct gov_attr_set *attr_set,
					char *buf)
{
	struct dugov_tunables *tunables = to_dugov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->iowait_boost_enable);
}

static ssize_t iowait_boost_enable_store(struct gov_attr_set *attr_set,
					 const char *buf, size_t count)
{
	struct dugov_tunables *tunables = to_dugov_tunables(attr_set);
	bool enable;

	if (kstrtobool(buf, &enable))
		return -EINVAL;

	tunables->iowait_boost_enable = enable;

	return count;
}

static ssize_t silver_suspend_max_freq_show(struct gov_attr_set *attr_set, char *buf)
{
	struct dugov_tunables *tunables = to_dugov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->silver_suspend_max_freq);
}

static ssize_t silver_suspend_max_freq_store(struct gov_attr_set *attr_set,
				      const char *buf, size_t count)
{
	struct dugov_tunables *tunables = to_dugov_tunables(attr_set);
	struct dugov_policy *du_policy;
	unsigned int max_freq;

	if (kstrtouint(buf, 10, &max_freq))
		return -EINVAL;

	if (max_freq > 0)
		cpufreq_driver_resolve_freq(du_policy->policy, max_freq);

	tunables->silver_suspend_max_freq = max_freq;

	return count;
}

static ssize_t gold_suspend_max_freq_show(struct gov_attr_set *attr_set, char *buf)
{
	struct dugov_tunables *tunables = to_dugov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->gold_suspend_max_freq);
}

static ssize_t gold_suspend_max_freq_store(struct gov_attr_set *attr_set,
				      const char *buf, size_t count)
{
	struct dugov_tunables *tunables = to_dugov_tunables(attr_set);
	struct dugov_policy *du_policy;
	unsigned int max_freq;

	if (kstrtouint(buf, 10, &max_freq))
		return -EINVAL;

	if (max_freq > 0)
		cpufreq_driver_resolve_freq(du_policy->policy, max_freq);

	tunables->gold_suspend_max_freq = max_freq;

	return count;
}

static ssize_t suspend_capacity_factor_show(struct gov_attr_set *attr_set, char *buf)
{
	struct dugov_tunables *tunables = to_dugov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->suspend_capacity_factor);
}

static ssize_t suspend_capacity_factor_store(struct gov_attr_set *attr_set,
				      const char *buf, size_t count)
{
	struct dugov_tunables *tunables = to_dugov_tunables(attr_set);
	unsigned int factor;

	if (kstrtouint(buf, 10, &factor))
		return -EINVAL;


	tunables->suspend_capacity_factor = factor;

	return count;
}

static ssize_t bit_shift1_show(struct gov_attr_set *attr_set, char *buf)
{
	struct dugov_tunables *tunables = to_dugov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->bit_shift1);
}

static ssize_t bit_shift2_show(struct gov_attr_set *attr_set, char *buf)
{
	struct dugov_tunables *tunables = to_dugov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->bit_shift2);
}

static ssize_t target_load1_show(struct gov_attr_set *attr_set, char *buf)
{
	struct dugov_tunables *tunables = to_dugov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->target_load1);
}

static ssize_t target_load2_show(struct gov_attr_set *attr_set, char *buf)
{
	struct dugov_tunables *tunables = to_dugov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->target_load2);
}

static ssize_t bit_shift1_store(struct gov_attr_set *attr_set,
					const char *buf, size_t count)
{
	struct dugov_tunables *tunables = to_dugov_tunables(attr_set);
	int value;

	if (kstrtouint(buf, 10, &value))
		return -EINVAL;

	value = min(max(0,value), 10);
	
	
	if (value == tunables->bit_shift1)
		return count;
		
	tunables->bit_shift1 = value;
	
	return count;
}

static ssize_t bit_shift2_store(struct gov_attr_set *attr_set,
					const char *buf, size_t count)
{
	struct dugov_tunables *tunables = to_dugov_tunables(attr_set);
	int value;

	if (kstrtouint(buf, 10, &value))
		return -EINVAL;

	value = min(max(0,value), 10);
	
	
	if (value == tunables->bit_shift2)
		return count;
		
	tunables->bit_shift2 = value;
	
	return count;
}

static ssize_t target_load1_store(struct gov_attr_set *attr_set,
					const char *buf, size_t count)
{
	struct dugov_tunables *tunables = to_dugov_tunables(attr_set);
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
	struct dugov_tunables *tunables = to_dugov_tunables(attr_set);
	int value;

	if (kstrtouint(buf, 10, &value))
		return -EINVAL;

	value = min(max(0,value), 100);
	
	
	if (value == tunables->target_load2)
		return count;
		
	tunables->target_load2 = value;
	
	return count;
}

static struct governor_attr up_rate_limit_us = __ATTR_RW(up_rate_limit_us);
static struct governor_attr down_rate_limit_us = __ATTR_RW(down_rate_limit_us);
static struct governor_attr iowait_boost_enable = __ATTR_RW(iowait_boost_enable);
static struct governor_attr silver_suspend_max_freq = __ATTR_RW(silver_suspend_max_freq);
static struct governor_attr gold_suspend_max_freq = __ATTR_RW(gold_suspend_max_freq);
static struct governor_attr suspend_capacity_factor = __ATTR_RW(suspend_capacity_factor);
static struct governor_attr bit_shift1 = __ATTR_RW(bit_shift1);
static struct governor_attr bit_shift2 = __ATTR_RW(bit_shift2);
static struct governor_attr target_load1 = __ATTR_RW(target_load1);
static struct governor_attr target_load2 = __ATTR_RW(target_load2);

static struct attribute *dugov_attributes[] = {
	&up_rate_limit_us.attr,
	&down_rate_limit_us.attr,
	&iowait_boost_enable.attr,
	&silver_suspend_max_freq.attr,
	&gold_suspend_max_freq.attr,
	&suspend_capacity_factor.attr,
	&bit_shift1.attr,
	&bit_shift2.attr,
	&target_load1.attr,
	&target_load2.attr,
	NULL
};

static struct kobj_type dugov_tunables_ktype = {
	.default_attrs = dugov_attributes,
	.sysfs_ops = &governor_sysfs_ops,
};

/********************** cpufreq governor interface *********************/
#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_DARKUTIL
static
#endif
struct cpufreq_governor cpufreq_gov_darkutil;

static struct dugov_policy *dugov_policy_alloc(struct cpufreq_policy *policy)
{
	struct dugov_policy *du_policy;

	du_policy = kzalloc(sizeof(*du_policy), GFP_KERNEL);
	if (!du_policy)
		return NULL;

	du_policy->policy = policy;
	init_irq_work(&du_policy->irq_work, dugov_irq_work);
	mutex_init(&du_policy->work_lock);
	raw_spin_lock_init(&du_policy->update_lock);
	return du_policy;
}

static void dugov_policy_free(struct dugov_policy *du_policy)
{
	mutex_destroy(&du_policy->work_lock);
	kfree(du_policy);
}

static int dugov_kthread_create(struct dugov_policy *du_policy)
{
	struct task_struct *thread;
	struct sched_param param = { .sched_priority = MAX_USER_RT_PRIO / 2 };
	struct cpufreq_policy *policy = du_policy->policy;
	int ret;

	/* kthread only required for slow path */
	if (policy->fast_switch_enabled)
		return 0;

	init_kthread_work(&du_policy->work, dugov_work);
	init_kthread_worker(&du_policy->worker);
	thread = kthread_create(kthread_worker_fn, &du_policy->worker,
				"dugov:%d",
				cpumask_first(policy->related_cpus));
	if (IS_ERR(thread)) {
		pr_err("failed to create eugov thread: %ld\n", PTR_ERR(thread));
		return PTR_ERR(thread);
	}

	ret = sched_setscheduler_nocheck(thread, SCHED_FIFO, &param);
	if (ret) {
		kthread_stop(thread);
		pr_warn("%s: failed to set SCHED_FIFO\n", __func__);
		return ret;
	}

	du_policy->thread = thread;
	kthread_bind_mask(thread, policy->related_cpus);
	wake_up_process(thread);

	return 0;
}

static void dugov_kthread_stop(struct dugov_policy *du_policy)
{
	/* kthread only required for slow path */
	if (du_policy->policy->fast_switch_enabled)
		return;

	flush_kthread_worker(&du_policy->worker);
	kthread_stop(du_policy->thread);
}

static struct dugov_tunables *dugov_tunables_alloc(struct dugov_policy *du_policy)
{
	struct dugov_tunables *tunables;

	tunables = kzalloc(sizeof(*tunables), GFP_KERNEL);
	if (tunables) {
		gov_attr_set_init(&tunables->attr_set, &du_policy->tunables_hook);
		if (!have_governor_per_policy())
			global_tunables = tunables;
	}
	return tunables;
}

static void dugov_tunables_free(struct dugov_tunables *tunables)
{
	if (!have_governor_per_policy())
		global_tunables = NULL;

	kfree(tunables);
}

static int dugov_init(struct cpufreq_policy *policy)
{
	unsigned int cpu = cpumask_first(policy->related_cpus);
	struct dugov_policy *du_policy;
	struct dugov_tunables *tunables;
	int ret = 0;

	/* State should be equivalent to EXIT */
	if (policy->governor_data)
		return -EBUSY;

	cpufreq_enable_fast_switch(policy);

	du_policy = dugov_policy_alloc(policy);
	if (!du_policy) {
		ret = -ENOMEM;
		goto disable_fast_switch;
	}

	ret = dugov_kthread_create(du_policy);
	if (ret)
		goto free_du_policy;

	mutex_lock(&global_tunables_lock);

	if (global_tunables) {
		if (WARN_ON(have_governor_per_policy())) {
			ret = -EINVAL;
			goto stop_kthread;
		}
		policy->governor_data = du_policy;
		du_policy->tunables = global_tunables;

		gov_attr_set_get(&global_tunables->attr_set, &du_policy->tunables_hook);
		goto out;
	}

	tunables = dugov_tunables_alloc(du_policy);
	if (!tunables) {
		ret = -ENOMEM;
		goto stop_kthread;
	}

	if (policy->up_transition_delay_us && policy->down_transition_delay_us) {
		tunables->up_rate_limit_us = policy->up_transition_delay_us;
		tunables->down_rate_limit_us = policy->down_transition_delay_us;
	} else {
		unsigned int lat;

                tunables->up_rate_limit_us = LATENCY_MULTIPLIER;
                tunables->down_rate_limit_us = LATENCY_MULTIPLIER;
		lat = policy->cpuinfo.transition_latency / NSEC_PER_USEC;
		if (lat) {
                        tunables->up_rate_limit_us *= lat;
                        tunables->down_rate_limit_us *= lat;
                }
	}

tunables->iowait_boost_enable = policy->iowait_boost_enable;
	tunables->silver_suspend_max_freq = DEFAULT_SUSPEND_MAX_FREQ_SILVER;
	tunables->gold_suspend_max_freq = DEFAULT_SUSPEND_MAX_FREQ_GOLD;
	tunables->suspend_capacity_factor = DEFAULT_SUSPEND_CAPACITY_FACTOR;

	if (cpu < 2){
		tunables->up_rate_limit_us = UP_RATE_LIMIT;
		tunables->down_rate_limit_us = DOWN_RATE_LIMIT;
		tunables->bit_shift1 = BIT_SHIFT_1;
		tunables->bit_shift2 = BIT_SHIFT_2;
		tunables->target_load1 = TARGET_LOAD_1;
		tunables->target_load2 = TARGET_LOAD_2;
	} else {
		tunables->up_rate_limit_us = UP_RATE_LIMIT_BIGC;
		tunables->down_rate_limit_us = DOWN_RATE_LIMIT_BIGC;
		tunables->bit_shift1 = BIT_SHIFT_1_BIGC;
		tunables->bit_shift2 = BIT_SHIFT_2_BIGC;
		tunables->target_load1 = TARGET_LOAD_1_BIGC;
		tunables->target_load2 = TARGET_LOAD_2_BIGC;
	}

	policy->governor_data = du_policy;
	stale_ns = walt_ravg_window + (walt_ravg_window >> 3);
	du_policy->tunables = tunables;

	ret = kobject_init_and_add(&tunables->attr_set.kobj, &dugov_tunables_ktype,
				   get_governor_parent_kobj(policy), "%s",
				   cpufreq_gov_darkutil.name);
	if (ret)
		goto fail;

out:
	mutex_unlock(&global_tunables_lock);
	return 0;

fail:
	policy->governor_data = NULL;
	dugov_tunables_free(tunables);

stop_kthread:
	dugov_kthread_stop(du_policy);

free_du_policy:
	mutex_unlock(&global_tunables_lock);

	dugov_policy_free(du_policy);

disable_fast_switch:
	cpufreq_disable_fast_switch(policy);

	pr_err("initialization failed (error %d)\n", ret);
	return ret;
}

static int dugov_exit(struct cpufreq_policy *policy)
{
	struct dugov_policy *du_policy = policy->governor_data;
	struct dugov_tunables *tunables = du_policy->tunables;
	unsigned int count;

	mutex_lock(&global_tunables_lock);

	count = gov_attr_set_put(&tunables->attr_set, &du_policy->tunables_hook);
	policy->governor_data = NULL;
	if (!count)
		dugov_tunables_free(tunables);

	mutex_unlock(&global_tunables_lock);

	dugov_kthread_stop(du_policy);
	dugov_policy_free(du_policy);

	cpufreq_disable_fast_switch(policy);
	return 0;
}

static int dugov_start(struct cpufreq_policy *policy)
{
	struct dugov_policy *du_policy = policy->governor_data;
	unsigned int cpu;

	du_policy->up_rate_delay_ns =
		du_policy->tunables->up_rate_limit_us * NSEC_PER_USEC;
	du_policy->down_rate_delay_ns =
		du_policy->tunables->down_rate_limit_us * NSEC_PER_USEC;
	update_min_rate_limit_us(du_policy);
	du_policy->last_freq_update_time = 0;
	du_policy->next_freq = 0;
	du_policy->work_in_progress = false;
	du_policy->need_freq_update = false;
	du_policy->cached_raw_freq = 0;

	for_each_cpu(cpu, policy->cpus) {
		struct dugov_cpu *du_cpu = &per_cpu(dugov_cpu, cpu);

		memset(du_cpu, 0, sizeof(*du_cpu));
		du_cpu->du_policy = du_policy;
		du_cpu->flags = SCHED_CPUFREQ_DL;
		du_cpu->iowait_boost_max = policy->cpuinfo.max_freq;
	}

	for_each_cpu(cpu, policy->cpus) {
		struct dugov_cpu *du_cpu = &per_cpu(dugov_cpu, cpu);

		cpufreq_add_update_util_hook(cpu, &du_cpu->update_util,
					     policy_is_shared(policy) ?
							dugov_update_shared :
							dugov_update_single);
	}
	return 0;
}

static int dugov_stop(struct cpufreq_policy *policy)
{
	struct dugov_policy *du_policy = policy->governor_data;
	unsigned int cpu;

	for_each_cpu(cpu, policy->cpus)
		cpufreq_remove_update_util_hook(cpu);

	synchronize_sched();

	if (!policy->fast_switch_enabled) {
		irq_work_sync(&du_policy->irq_work);
		kthread_cancel_work_sync(&du_policy->work);
	}
	return 0;
}

static int dugov_limits(struct cpufreq_policy *policy)
{
	struct dugov_policy *du_policy = policy->governor_data;

	if (!policy->fast_switch_enabled) {
		mutex_lock(&du_policy->work_lock);
		cpufreq_policy_apply_limits(policy);
		mutex_unlock(&du_policy->work_lock);
	}

	du_policy->need_freq_update = true;

	return 0;
}

static int cpufreq_darkutil_cb(struct cpufreq_policy *policy,
				unsigned int event)
{
	switch(event) {
	case CPUFREQ_GOV_POLICY_INIT:
		return dugov_init(policy);
	case CPUFREQ_GOV_POLICY_EXIT:
		return dugov_exit(policy);
	case CPUFREQ_GOV_START:
		return dugov_start(policy);
	case CPUFREQ_GOV_STOP:
		return dugov_stop(policy);
	case CPUFREQ_GOV_LIMITS:
		return dugov_limits(policy);
	default:
		BUG();
	}
}

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_DARKUTIL
static
#endif
struct cpufreq_governor cpufreq_gov_darkutil = {
	.name = "darkutil",
	.governor = cpufreq_darkutil_cb,
	.owner = THIS_MODULE,
};

static int __init dugov_register(void)
{
	return cpufreq_register_governor(&cpufreq_gov_darkutil);
}
fs_initcall(dugov_register);
