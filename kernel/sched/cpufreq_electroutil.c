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
#define LATENCY_MULTIPLIER			(1000)
#define EUGOV_KTHREAD_PRIORITY	50
#define DEFAULT_SUSPEND_MAX_FREQ 0
#define DEFAULT_SUSPEND_CAPACITY_FACTOR 10

struct eugov_tunables {
	struct gov_attr_set attr_set;
	unsigned int up_rate_limit_us;
	unsigned int down_rate_limit_us;
	bool iowait_boost_enable;
	unsigned int silver_suspend_max_freq;
	unsigned int gold_suspend_max_freq;
	unsigned int suspend_capacity_factor;
};

struct eugov_policy {
	struct cpufreq_policy *policy;

	struct eugov_tunables *tunables;
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

struct eugov_cpu {
	struct update_util_data update_util;
	struct eugov_policy *eg_policy;

	bool iowait_boost_pending;
	unsigned int iowait_boost;
	unsigned int iowait_boost_max;
	u64 last_update;

	/* The fields below are only needed when sharing a policy. */
	unsigned long util;
	unsigned long max;
	unsigned int flags;

	/* The field below is for single-CPU policies only. */
#ifdef CONFIG_NO_HZ_COMMON
	unsigned long saved_idle_calls;
#endif
};

static DEFINE_PER_CPU(struct eugov_cpu, eugov_cpu);

/************************ Governor internals ***********************/

static bool eugov_should_update_freq(struct eugov_policy *eg_policy, u64 time)
{
	s64 delta_ns;

	if (eg_policy->work_in_progress)
		return false;

	if (unlikely(eg_policy->need_freq_update)) {
		eg_policy->need_freq_update = false;
		/*
		 * This happens when limits change, so forget the previous
		 * next_freq value and force an update.
		 */
		eg_policy->next_freq = UINT_MAX;
		return true;
	}

	delta_ns = time - eg_policy->last_freq_update_time;

	/* No need to recalculate next freq for min_rate_limit_us at least */
	return delta_ns >= eg_policy->min_rate_limit_ns;
}

static bool eugov_up_down_rate_limit(struct eugov_policy *eg_policy, u64 time,
				     unsigned int next_freq)
{
	s64 delta_ns;

	delta_ns = time - eg_policy->last_freq_update_time;

	if (next_freq > eg_policy->next_freq &&
	    delta_ns < eg_policy->up_rate_delay_ns)
			return true;

	if (next_freq < eg_policy->next_freq &&
	    delta_ns < eg_policy->down_rate_delay_ns)
			return true;

	return false;
}

static void eugov_update_commit(struct eugov_policy *eg_policy, u64 time,
				unsigned int next_freq)
{
	struct cpufreq_policy *policy = eg_policy->policy;

	if (eugov_up_down_rate_limit(eg_policy, time, next_freq))
		return;

	if (eg_policy->next_freq == next_freq)
		return;

	eg_policy->next_freq = next_freq;
	eg_policy->last_freq_update_time = time;

	if (policy->fast_switch_enabled) {
		next_freq = cpufreq_driver_fast_switch(policy, next_freq);
		if (next_freq == CPUFREQ_ENTRY_INVALID)
			return;

		policy->cur = next_freq;
		trace_cpu_frequency(next_freq, smp_processor_id());
	} else {
		eg_policy->work_in_progress = true;
		irq_work_queue(&eg_policy->irq_work);
	}
}

/**
 * get_next_freq - Compute a new frequency for a given cpufreq policy.
 * @eg_policy: electroutil policy object to compute the new frequency for.
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
static unsigned int get_next_freq(struct eugov_policy *eg_policy,
				  unsigned long util, unsigned long max)
{
	struct cpufreq_policy *policy = eg_policy->policy;
	unsigned int freq = arch_scale_freq_invariant() ?
				policy->cpuinfo.max_freq : policy->cur;
	unsigned int capacity_factor, silver_max_freq, gold_max_freq;

	if(state_suspended) {
		capacity_factor = eg_policy->tunables->suspend_capacity_factor;
		silver_max_freq = eg_policy->tunables->silver_suspend_max_freq;
		gold_max_freq = eg_policy->tunables->gold_suspend_max_freq;
		max = max * (capacity_factor + 1) / capacity_factor;
	}

	switch(policy->cpu){
	case 0:
	case 1:
	case 2:
	case 3:
		freq = (freq + (freq >> 2)) * util / max;
		if(state_suspended &&  silver_max_freq > 0 && silver_max_freq < freq)
			return silver_max_freq;
		break;
	case 4:
	case 5:
		freq = (freq + (freq >> 2)) * util / max;
		if(state_suspended && gold_max_freq > 0 && gold_max_freq < freq)
			return gold_max_freq;
		break;
	case 6:
	case 7:
		if(state_suspended)
			return policy->min;
		else
			freq = freq * util / max;
		break;
	default:
		BUG();
	}

	if (freq == eg_policy->cached_raw_freq && eg_policy->next_freq != UINT_MAX)
		return eg_policy->next_freq;
	eg_policy->cached_raw_freq = freq;
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

static void eugov_get_util(unsigned long *util, unsigned long *max, u64 time)
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

	*util = boosted_cpu_util(cpu);
	if (likely(use_pelt()))
		*util = *util + rt;

	*util = min(*util, max_cap);
	*max = max_cap;
}

static void eugov_set_iowait_boost(struct eugov_cpu *eg_cpu, u64 time,
				   unsigned int flags)
{
	struct eugov_policy *eg_policy = eg_cpu->eg_policy;

	if (!eg_policy->tunables->iowait_boost_enable)
		return;

	if (flags & SCHED_CPUFREQ_IOWAIT) {
		if (eg_cpu->iowait_boost_pending)
			return;

		eg_cpu->iowait_boost_pending = true;

		if (eg_cpu->iowait_boost) {
			eg_cpu->iowait_boost <<= 1;
			if (eg_cpu->iowait_boost > eg_cpu->iowait_boost_max)
				eg_cpu->iowait_boost = eg_cpu->iowait_boost_max;
		} else {
			eg_cpu->iowait_boost = eg_cpu->eg_policy->policy->min;
		}
	} else if (eg_cpu->iowait_boost) {
		s64 delta_ns = time - eg_cpu->last_update;

		/* Clear iowait_boost if the CPU apprears to have been idle. */
		if (delta_ns > TICK_NSEC) {
			eg_cpu->iowait_boost = 0;
			eg_cpu->iowait_boost_pending = false;
		}
	}
}

static void eugov_iowait_boost(struct eugov_cpu *eg_cpu, unsigned long *util,
			       unsigned long *max)
{
	unsigned int boost_util, boost_max;

	if (!eg_cpu->iowait_boost)
		return;

	if (eg_cpu->iowait_boost_pending) {
		eg_cpu->iowait_boost_pending = false;
	} else {
		eg_cpu->iowait_boost >>= 1;
		if (eg_cpu->iowait_boost < eg_cpu->eg_policy->policy->min) {
			eg_cpu->iowait_boost = 0;
			return;
		}
	}

	boost_util = eg_cpu->iowait_boost;
	boost_max = eg_cpu->iowait_boost_max;

	if (*util * boost_max < *max * boost_util) {
		*util = boost_util;
		*max = boost_max;
	}
}

#ifdef CONFIG_NO_HZ_COMMON
static bool eugov_cpu_is_busy(struct eugov_cpu *eg_cpu)
{
	unsigned long idle_calls = tick_nohz_get_idle_calls();
	bool ret = idle_calls == eg_cpu->saved_idle_calls;

	eg_cpu->saved_idle_calls = idle_calls;
	return ret;
}
#else
static inline bool eugov_cpu_is_busy(struct eugov_cpu *eg_cpu) { return false; }
#endif /* CONFIG_NO_HZ_COMMON */

static void eugov_update_single(struct update_util_data *hook, u64 time,
				unsigned int flags)
{
	struct eugov_cpu *eg_cpu = container_of(hook, struct eugov_cpu, update_util);
	struct eugov_policy *eg_policy = eg_cpu->eg_policy;
	struct cpufreq_policy *policy = eg_policy->policy;
	unsigned long util, max;
	unsigned int next_f;
	bool busy;

	eugov_set_iowait_boost(eg_cpu, time, flags);
	eg_cpu->last_update = time;

	if (!eugov_should_update_freq(eg_policy, time))
		return;

	busy = eugov_cpu_is_busy(eg_cpu);

	if (flags & SCHED_CPUFREQ_DL) {
		next_f = policy->cpuinfo.max_freq;
	} else {
		eugov_get_util(&util, &max, time);
		eugov_iowait_boost(eg_cpu, &util, &max);
		next_f = get_next_freq(eg_policy, util, max);
		/*
		 * Do not reduce the frequency if the CPU has not been idle
		 * recently, as the reduction is likely to be premature then.
		 */
		if (busy && next_f < eg_policy->next_freq)
			next_f = eg_policy->next_freq;
	}
	eugov_update_commit(eg_policy, time, next_f);
}

static unsigned int eugov_next_freq_shared(struct eugov_cpu *eg_cpu, u64 time)
{
	struct eugov_policy *eg_policy = eg_cpu->eg_policy;
	struct cpufreq_policy *policy = eg_policy->policy;
	unsigned long util = 0, max = 1;
	unsigned int j;

	for_each_cpu(j, policy->cpus) {
		struct eugov_cpu *j_eg_cpu = &per_cpu(eugov_cpu, j);
		unsigned long j_util, j_max;
		s64 delta_ns;

		/*
		 * If the CPU utilization was last updated before the previous
		 * frequency update and the time elapsed between the last update
		 * of the CPU utilization and the last frequency update is long
		 * enough, don't take the CPU into account as it probably is
		 * idle now (and clear iowait_boost for it).
		 */
		delta_ns = time - j_eg_cpu->last_update;
		if (delta_ns > TICK_NSEC) {
			j_eg_cpu->iowait_boost = 0;
			j_eg_cpu->iowait_boost_pending = false;
			continue;
		}
		if (j_eg_cpu->flags & SCHED_CPUFREQ_DL)
			return policy->cpuinfo.max_freq;

		j_util = j_eg_cpu->util;
		j_max = j_eg_cpu->max;
		if (j_util * max > j_max * util) {
			util = j_util;
			max = j_max;
		}

		eugov_iowait_boost(j_eg_cpu, &util, &max);
	}

	return get_next_freq(eg_policy, util, max);
}

static void eugov_update_shared(struct update_util_data *hook, u64 time,
				unsigned int flags)
{
	struct eugov_cpu *eg_cpu = container_of(hook, struct eugov_cpu, update_util);
	struct eugov_policy *eg_policy = eg_cpu->eg_policy;
	unsigned long util, max;
	unsigned int next_f;

	eugov_get_util(&util, &max, time);

	raw_spin_lock(&eg_policy->update_lock);

	eg_cpu->util = util;
	eg_cpu->max = max;
	eg_cpu->flags = flags;

	eugov_set_iowait_boost(eg_cpu, time, flags);
	eg_cpu->last_update = time;

	if (eugov_should_update_freq(eg_policy, time)) {
		if (flags & SCHED_CPUFREQ_DL)
			next_f = eg_policy->policy->cpuinfo.max_freq;
		else
			next_f = eugov_next_freq_shared(eg_cpu, time);

		eugov_update_commit(eg_policy, time, next_f);
	}

	raw_spin_unlock(&eg_policy->update_lock);
}

static void eugov_work(struct kthread_work *work)
{
	struct eugov_policy *eg_policy = container_of(work, struct eugov_policy, work);

	mutex_lock(&eg_policy->work_lock);
	__cpufreq_driver_target(eg_policy->policy, eg_policy->next_freq,
				CPUFREQ_RELATION_L);
	mutex_unlock(&eg_policy->work_lock);

	eg_policy->work_in_progress = false;
}

static void eugov_irq_work(struct irq_work *irq_work)
{
	struct eugov_policy *eg_policy;

	eg_policy = container_of(irq_work, struct eugov_policy, irq_work);

	/*
	 * For RT and deadline tasks, the electroutil governor shoots the
	 * frequency to maximum. Special care must be taken to ensure that this
	 * kthread doesn't result in the same behavior.
	 *
	 * This is (mostly) guaranteed by the work_in_progress flag. The flag is
	 * updated only at the end of the eugov_work() function and before that
	 * the electroutil governor rejects all other frequency scaling requests.
	 *
	 * There is a very rare case though, where the RT thread yields right
	 * after the work_in_progress flag is cleared. The effects of that are
	 * neglected for now.
	 */
	queue_kthread_work(&eg_policy->worker, &eg_policy->work);
}

/************************** sysfs interface ************************/

static struct eugov_tunables *global_tunables;
static DEFINE_MUTEX(global_tunables_lock);

static inline struct eugov_tunables *to_eugov_tunables(struct gov_attr_set *attr_set)
{
	return container_of(attr_set, struct eugov_tunables, attr_set);
}

static DEFINE_MUTEX(min_rate_lock);

static void update_min_rate_limit_us(struct eugov_policy *eg_policy)
{
	mutex_lock(&min_rate_lock);
	eg_policy->min_rate_limit_ns = min(eg_policy->up_rate_delay_ns,
					   eg_policy->down_rate_delay_ns);
	mutex_unlock(&min_rate_lock);
}

static ssize_t up_rate_limit_us_show(struct gov_attr_set *attr_set, char *buf)
{
	struct eugov_tunables *tunables = to_eugov_tunables(attr_set);

	return sprintf(buf, "%u\n", tunables->up_rate_limit_us);
}

static ssize_t down_rate_limit_us_show(struct gov_attr_set *attr_set, char *buf)
{
	struct eugov_tunables *tunables = to_eugov_tunables(attr_set);

	return sprintf(buf, "%u\n", tunables->down_rate_limit_us);
}

static ssize_t up_rate_limit_us_store(struct gov_attr_set *attr_set,
				      const char *buf, size_t count)
{
	struct eugov_tunables *tunables = to_eugov_tunables(attr_set);
	struct eugov_policy *eg_policy;
	unsigned int rate_limit_us;

	if (kstrtouint(buf, 10, &rate_limit_us))
		return -EINVAL;

	tunables->up_rate_limit_us = rate_limit_us;

	list_for_each_entry(eg_policy, &attr_set->policy_list, tunables_hook) {
		eg_policy->up_rate_delay_ns = rate_limit_us * NSEC_PER_USEC;
		update_min_rate_limit_us(eg_policy);
	}

	return count;
}

static ssize_t down_rate_limit_us_store(struct gov_attr_set *attr_set,
					const char *buf, size_t count)
{
	struct eugov_tunables *tunables = to_eugov_tunables(attr_set);
	struct eugov_policy *eg_policy;
	unsigned int rate_limit_us;

	if (kstrtouint(buf, 10, &rate_limit_us))
		return -EINVAL;

	tunables->down_rate_limit_us = rate_limit_us;

	list_for_each_entry(eg_policy, &attr_set->policy_list, tunables_hook) {
		eg_policy->down_rate_delay_ns = rate_limit_us * NSEC_PER_USEC;
		update_min_rate_limit_us(eg_policy);
	}

	return count;
}

static ssize_t iowait_boost_enable_show(struct gov_attr_set *attr_set,
					char *buf)
{
	struct eugov_tunables *tunables = to_eugov_tunables(attr_set);

	return sprintf(buf, "%u\n", tunables->iowait_boost_enable);
}

static ssize_t iowait_boost_enable_store(struct gov_attr_set *attr_set,
					 const char *buf, size_t count)
{
	struct eugov_tunables *tunables = to_eugov_tunables(attr_set);
	bool enable;

	if (kstrtobool(buf, &enable))
		return -EINVAL;

	tunables->iowait_boost_enable = enable;

	return count;
}

static ssize_t silver_suspend_max_freq_show(struct gov_attr_set *attr_set, char *buf)
{
	struct eugov_tunables *tunables = to_eugov_tunables(attr_set);

	return sprintf(buf, "%u\n", tunables->silver_suspend_max_freq);
}

static ssize_t silver_suspend_max_freq_store(struct gov_attr_set *attr_set,
				      const char *buf, size_t count)
{
	struct eugov_tunables *tunables = to_eugov_tunables(attr_set);
	struct eugov_policy *eg_policy;
	unsigned int max_freq;

	if (kstrtouint(buf, 10, &max_freq))
		return -EINVAL;

	if (max_freq > 0)
		cpufreq_driver_resolve_freq(eg_policy->policy, max_freq);

	tunables->silver_suspend_max_freq = max_freq;

	return count;
}

static ssize_t gold_suspend_max_freq_show(struct gov_attr_set *attr_set, char *buf)
{
	struct eugov_tunables *tunables = to_eugov_tunables(attr_set);

	return sprintf(buf, "%u\n", tunables->gold_suspend_max_freq);
}

static ssize_t gold_suspend_max_freq_store(struct gov_attr_set *attr_set,
				      const char *buf, size_t count)
{
	struct eugov_tunables *tunables = to_eugov_tunables(attr_set);
	struct eugov_policy *eg_policy;
	unsigned int max_freq;

	if (kstrtouint(buf, 10, &max_freq))
		return -EINVAL;

	if (max_freq > 0)
		cpufreq_driver_resolve_freq(eg_policy->policy, max_freq);

	tunables->gold_suspend_max_freq = max_freq;

	return count;
}

static ssize_t suspend_capacity_factor_show(struct gov_attr_set *attr_set, char *buf)
{
	struct eugov_tunables *tunables = to_eugov_tunables(attr_set);

	return sprintf(buf, "%u\n", tunables->suspend_capacity_factor);
}

static ssize_t suspend_capacity_factor_store(struct gov_attr_set *attr_set,
				      const char *buf, size_t count)
{
	struct eugov_tunables *tunables = to_eugov_tunables(attr_set);
	struct eugov_policy *eg_policy;
	unsigned int factor;

	if (kstrtouint(buf, 10, &factor))
		return -EINVAL;


	tunables->suspend_capacity_factor = factor;

	return count;
}

static struct governor_attr up_rate_limit_us = __ATTR_RW(up_rate_limit_us);
static struct governor_attr down_rate_limit_us = __ATTR_RW(down_rate_limit_us);
static struct governor_attr iowait_boost_enable = __ATTR_RW(iowait_boost_enable);
static struct governor_attr silver_suspend_max_freq = __ATTR_RW(silver_suspend_max_freq);
static struct governor_attr gold_suspend_max_freq = __ATTR_RW(gold_suspend_max_freq);
static struct governor_attr suspend_capacity_factor = __ATTR_RW(suspend_capacity_factor);

static struct attribute *eugov_attributes[] = {
	&up_rate_limit_us.attr,
	&down_rate_limit_us.attr,
	&iowait_boost_enable.attr,
	&silver_suspend_max_freq.attr,
	&gold_suspend_max_freq.attr,
	&suspend_capacity_factor.attr,
	NULL
};

static struct kobj_type eugov_tunables_ktype = {
	.default_attrs = eugov_attributes,
	.sysfs_ops = &governor_sysfs_ops,
};

/********************** cpufreq governor interface *********************/
#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_ELECTROUTIL
static
#endif
struct cpufreq_governor cpufreq_gov_electroutil;

static struct eugov_policy *eugov_policy_alloc(struct cpufreq_policy *policy)
{
	struct eugov_policy *eg_policy;

	eg_policy = kzalloc(sizeof(*eg_policy), GFP_KERNEL);
	if (!eg_policy)
		return NULL;

	eg_policy->policy = policy;
	raw_spin_lock_init(&eg_policy->update_lock);
	return eg_policy;
}

static void eugov_policy_free(struct eugov_policy *eg_policy)
{
	kfree(eg_policy);
}

static int eugov_kthread_create(struct eugov_policy *eg_policy)
{
	struct task_struct *thread;
	struct sched_param param = { .sched_priority = MAX_USER_RT_PRIO / 2 };
	struct cpufreq_policy *policy = eg_policy->policy;
	int ret;

	/* kthread only required for slow path */
	if (policy->fast_switch_enabled)
		return 0;

	init_kthread_work(&eg_policy->work, eugov_work);
	init_kthread_worker(&eg_policy->worker);
	thread = kthread_create(kthread_worker_fn, &eg_policy->worker,
				"eugov:%d",
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

	eg_policy->thread = thread;
	kthread_bind_mask(thread, policy->related_cpus);
	init_irq_work(&eg_policy->irq_work, eugov_irq_work);
	mutex_init(&eg_policy->work_lock);

	wake_up_process(thread);

	return 0;
}

static void eugov_kthread_stop(struct eugov_policy *eg_policy)
{
	/* kthread only required for slow path */
	if (eg_policy->policy->fast_switch_enabled)
		return;

	flush_kthread_worker(&eg_policy->worker);
	kthread_stop(eg_policy->thread);
	mutex_destroy(&eg_policy->work_lock);
}

static struct eugov_tunables *eugov_tunables_alloc(struct eugov_policy *eg_policy)
{
	struct eugov_tunables *tunables;

	tunables = kzalloc(sizeof(*tunables), GFP_KERNEL);
	if (tunables) {
		gov_attr_set_init(&tunables->attr_set, &eg_policy->tunables_hook);
		if (!have_governor_per_policy())
			global_tunables = tunables;
	}
	return tunables;
}

static void eugov_tunables_free(struct eugov_tunables *tunables)
{
	if (!have_governor_per_policy())
		global_tunables = NULL;

	kfree(tunables);
}

static int eugov_init(struct cpufreq_policy *policy)
{
	struct eugov_policy *eg_policy;
	struct eugov_tunables *tunables;
	int ret = 0;

	/* State should be equivalent to EXIT */
	if (policy->governor_data)
		return -EBUSY;

	cpufreq_enable_fast_switch(policy);

	eg_policy = eugov_policy_alloc(policy);
	if (!eg_policy) {
		ret = -ENOMEM;
		goto disable_fast_switch;
	}

	ret = eugov_kthread_create(eg_policy);
	if (ret)
		goto free_eg_policy;

	mutex_lock(&global_tunables_lock);

	if (global_tunables) {
		if (WARN_ON(have_governor_per_policy())) {
			ret = -EINVAL;
			goto stop_kthread;
		}
		policy->governor_data = eg_policy;
		eg_policy->tunables = global_tunables;

		gov_attr_set_get(&global_tunables->attr_set, &eg_policy->tunables_hook);
		goto out;
	}

	tunables = eugov_tunables_alloc(eg_policy);
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

	tunables->silver_suspend_max_freq = DEFAULT_SUSPEND_MAX_FREQ;
	tunables->gold_suspend_max_freq = DEFAULT_SUSPEND_MAX_FREQ;
	tunables->suspend_capacity_factor = DEFAULT_SUSPEND_CAPACITY_FACTOR;

	policy->governor_data = eg_policy;
	eg_policy->tunables = tunables;

	ret = kobject_init_and_add(&tunables->attr_set.kobj, &eugov_tunables_ktype,
				   get_governor_parent_kobj(policy), "%s",
				   cpufreq_gov_electroutil.name);
	if (ret)
		goto fail;

out:
	mutex_unlock(&global_tunables_lock);
	return 0;

fail:
	policy->governor_data = NULL;
	eugov_tunables_free(tunables);

stop_kthread:
	eugov_kthread_stop(eg_policy);

free_eg_policy:
	mutex_unlock(&global_tunables_lock);

	eugov_policy_free(eg_policy);

disable_fast_switch:
	cpufreq_disable_fast_switch(policy);

	pr_err("initialization failed (error %d)\n", ret);
	return ret;
}

static int eugov_exit(struct cpufreq_policy *policy)
{
	struct eugov_policy *eg_policy = policy->governor_data;
	struct eugov_tunables *tunables = eg_policy->tunables;
	unsigned int count;

	mutex_lock(&global_tunables_lock);

	count = gov_attr_set_put(&tunables->attr_set, &eg_policy->tunables_hook);
	policy->governor_data = NULL;
	if (!count)
		eugov_tunables_free(tunables);

	mutex_unlock(&global_tunables_lock);

	eugov_kthread_stop(eg_policy);
	eugov_policy_free(eg_policy);

	cpufreq_disable_fast_switch(policy);
	return 0;
}

static int eugov_start(struct cpufreq_policy *policy)
{
	struct eugov_policy *eg_policy = policy->governor_data;
	unsigned int cpu;

	eg_policy->up_rate_delay_ns =
		eg_policy->tunables->up_rate_limit_us * NSEC_PER_USEC;
	eg_policy->down_rate_delay_ns =
		eg_policy->tunables->down_rate_limit_us * NSEC_PER_USEC;
	update_min_rate_limit_us(eg_policy);
	eg_policy->last_freq_update_time = 0;
	eg_policy->next_freq = UINT_MAX;
	eg_policy->work_in_progress = false;
	eg_policy->need_freq_update = false;
	eg_policy->cached_raw_freq = 0;

	for_each_cpu(cpu, policy->cpus) {
		struct eugov_cpu *eg_cpu = &per_cpu(eugov_cpu, cpu);

		memset(eg_cpu, 0, sizeof(*eg_cpu));
		eg_cpu->eg_policy = eg_policy;
		eg_cpu->flags = SCHED_CPUFREQ_DL;
		eg_cpu->iowait_boost_max = policy->cpuinfo.max_freq;
		cpufreq_add_update_util_hook(cpu, &eg_cpu->update_util,
					     policy_is_shared(policy) ?
							eugov_update_shared :
							eugov_update_single);
	}
	return 0;
}

static int eugov_stop(struct cpufreq_policy *policy)
{
	struct eugov_policy *eg_policy = policy->governor_data;
	unsigned int cpu;

	for_each_cpu(cpu, policy->cpus)
		cpufreq_remove_update_util_hook(cpu);

	synchronize_sched();

	if (!policy->fast_switch_enabled) {
		irq_work_sync(&eg_policy->irq_work);
		kthread_cancel_work_sync(&eg_policy->work);
	}
	return 0;
}

static int eugov_limits(struct cpufreq_policy *policy)
{
	struct eugov_policy *eg_policy = policy->governor_data;

	if (!policy->fast_switch_enabled) {
		mutex_lock(&eg_policy->work_lock);
		cpufreq_policy_apply_limits(policy);
		mutex_unlock(&eg_policy->work_lock);
	}

	eg_policy->need_freq_update = true;

	return 0;
}

static int cpufreq_electroutil_cb(struct cpufreq_policy *policy,
				unsigned int event)
{
	switch(event) {
	case CPUFREQ_GOV_POLICY_INIT:
		return eugov_init(policy);
	case CPUFREQ_GOV_POLICY_EXIT:
		return eugov_exit(policy);
	case CPUFREQ_GOV_START:
		return eugov_start(policy);
	case CPUFREQ_GOV_STOP:
		return eugov_stop(policy);
	case CPUFREQ_GOV_LIMITS:
		return eugov_limits(policy);
	default:
		BUG();
	}
}

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_ELECTROUTIL
static
#endif
struct cpufreq_governor cpufreq_gov_electroutil = {
	.name = "electroutil",
	.governor = cpufreq_electroutil_cb,
	.owner = THIS_MODULE,
};

static int __init eugov_register(void)
{
	return cpufreq_register_governor(&cpufreq_gov_electroutil);
}
fs_initcall(eugov_register);
