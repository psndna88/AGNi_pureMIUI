/*
 * Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
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
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/sort.h>
#include <linux/oom.h>
#include <linux/sched.h>
#include <linux/rcupdate.h>
#include <linux/notifier.h>
#include <linux/vmpressure.h>

#ifdef CONFIG_ANDROID_PR_KILL
#include <linux/delay.h>
#endif

#ifndef CONFIG_ANDROID_PR_KILL
#define CREATE_TRACE_POINTS
#include <trace/events/process_reclaim.h>
#endif

#define MAX_SWAP_TASKS SWAP_CLUSTER_MAX

static void swap_fn(struct work_struct *work);
DECLARE_WORK(swap_work, swap_fn);

#ifndef CONFIG_ANDROID_PR_KILL
/* User knob to enable/disable process reclaim feature */
static int enable_process_reclaim;
module_param_named(enable_process_reclaim, enable_process_reclaim, int, 0644);
#endif

/* The max number of pages tried to be reclaimed in a single run */
int per_swap_size = SWAP_CLUSTER_MAX * 32;
module_param_named(per_swap_size, per_swap_size, int, 0644);

int reclaim_avg_efficiency;
module_param_named(reclaim_avg_efficiency, reclaim_avg_efficiency, int, 0444);

/* The vmpressure region where process reclaim operates */
static unsigned long pressure_min = 50;
static unsigned long pressure_max = 90;
module_param_named(pressure_min, pressure_min, ulong, 0644);
module_param_named(pressure_max, pressure_max, ulong, 0644);

#ifndef CONFIG_ANDROID_PR_KILL
static short min_score_adj = 360;
module_param_named(min_score_adj, min_score_adj, short, 0644);
#endif

/*
 * Scheduling process reclaim workqueue unecessarily
 * when the reclaim efficiency is low does not make
 * sense. We try to detect a drop in efficiency and
 * disable reclaim for a time period. This period and the
 * period for which we monitor a drop in efficiency is
 * defined by swap_eff_win. swap_opt_eff is the optimal
 * efficincy used as theshold for this.
 */
static int swap_eff_win = 2;
module_param_named(swap_eff_win, swap_eff_win, int, 0644);

static int swap_opt_eff = 50;
module_param_named(swap_opt_eff, swap_opt_eff, int, 0644);

#ifdef CONFIG_ANDROID_PR_KILL
/*
 * OOM Killer will be called if the total number of
 * file pages (active) reaches this limit
 */
static int free_file_limit = 36000;
module_param_named(free_file_limit, free_file_limit, int, 0644);

/* Number of SWAP pages in MiB below which tasks should be killed */
static int free_swap_limit = 40;
module_param_named(free_swap_limit, free_swap_limit, int, 0644);

/* Minimum OOM score above which tasks should be killed */
static short score_kill_limit = 300;
module_param_named(score_kill_limit, score_kill_limit, short, 0644);
#endif

static atomic_t skip_reclaim = ATOMIC_INIT(0);
/* Not atomic since only a single instance of swap_fn run at a time */
static int monitor_eff;

struct selected_task {
	struct task_struct *p;
	int tasksize;
	short oom_score_adj;
};

#ifdef CONFIG_ANDROID_PR_KILL
static const short never_reclaim[] = {
	0, /* Foreground task */
	50,
	200 /* Running service */
};
#endif

int selected_cmp(const void *a, const void *b)
{
	const struct selected_task *x = a;
	const struct selected_task *y = b;
	int ret;

	ret = x->tasksize < y->tasksize ? -1 : 1;

	return ret;
}

#ifdef CONFIG_ANDROID_PR_KILL
int txpd_cmp(const void *a, const void *b)
{
	struct selected_task *x = ((struct selected_task *)a);
	struct selected_task *y = ((struct selected_task *)b);

	if (x->p->acct_timexpd < y->p->acct_timexpd)
		return 1;

	if (x->p->acct_timexpd > y->p->acct_timexpd)
		return -1;

	return 0;
}
#endif

static int test_task_flag(struct task_struct *p, int flag)
{
	struct task_struct *t = p;

	rcu_read_lock();
	for_each_thread(p, t) {
		task_lock(t);
		if (test_tsk_thread_flag(t, flag)) {
			task_unlock(t);
			rcu_read_unlock();
			return 1;
		}
		task_unlock(t);
	}
	rcu_read_unlock();

	return 0;
}

#ifdef CONFIG_ANDROID_PR_KILL
static int score_adj_check(short oom_score_adj)
{
	int i;
	short never_reclaim_size = ARRAY_SIZE(never_reclaim);

	for (i = 0; i < never_reclaim_size; i++)
		if (oom_score_adj == never_reclaim[i])
			return 1;

	return 0;
}

/*
 * Low-memory notification levels
 *
 * LOWMEM_NONE: No low-memory scenario detected.
 *
 * LOWMEM_NORMAL: A scenario in which the SWAP
 * memory levels are below defined thresholds.
 * (swap_mem as defined below)
 *
 * LOWMEM_CRITICAL: A scenario in which the LOWMEM_NORMAL
 * condition is satisfied, as well as when the reclaimable
 * file pages (active) are below a certain threshold.
 * (free_file_limit as defined above)
 */
enum lowmem_levels {
	LOWMEM_NONE,
	LOWMEM_NORMAL,
	LOWMEM_CRITICAL,
};

static int is_low_mem(void)
{
	const int lru_base = NR_LRU_BASE - LRU_BASE;

	unsigned long cur_file_mem =
			global_zone_page_state(lru_base + LRU_ACTIVE_FILE);

	unsigned long cur_swap_mem = (get_nr_swap_pages() << (PAGE_SHIFT - 10));
	unsigned long swap_mem = free_swap_limit * 1024;

	bool lowmem_normal = cur_swap_mem < swap_mem;
	bool lowmem_critical = lowmem_normal &&
				cur_file_mem < free_file_limit;

	if (lowmem_critical)
		return LOWMEM_CRITICAL;
	else if (lowmem_normal)
		return LOWMEM_NORMAL;
	else
		return LOWMEM_NONE;
}

static void sort_and_kill_tasks(struct selected_task selected[], int si)
{
	int i, j, max = si;

	/*
	 * We sort tasks based on (stime+utime) since last accessed,
	 * in descending order.
	 */
	sort(selected, si, sizeof(*selected), txpd_cmp, NULL);

	/* We kill tasks with the lowest (stime+utime) */
	while (si--) {
		struct task_struct *tsk = selected[si].p;

		if (is_low_mem() == LOWMEM_NONE)
			break;

		task_lock(tsk);
		send_sig(SIGKILL, tsk, 0);
		task_unlock(tsk);

		pr_debug("process_reclaim: total:%d[%d] comm:%s(%d) txpd:%llu KILLED!",
				max,
				(si + 1),
				tsk->comm,
				tsk->signal->oom_score_adj,
				tsk->acct_timexpd);

		msleep(20);
		if (tsk)
			put_task_struct(tsk);

	}
}
#endif

static void swap_fn(struct work_struct *work)
{
	struct task_struct *tsk;
	struct reclaim_param rp;

	/* Pick the best MAX_SWAP_TASKS tasks in terms of anon size */
	struct selected_task selected[MAX_SWAP_TASKS] = {{0, 0, 0},};
	int si = 0;
	int i;
	int tasksize;
	int total_sz = 0;
	int total_scan = 0;
	int total_reclaimed = 0;
	int nr_to_reclaim;
	int efficiency;

#ifdef CONFIG_ANDROID_PR_KILL
	/*
	 * In case memory is critically low, i.e at a
	 * LOWMEM_CRITICAL level as defined above lowmem_levels,
	 * we kill a memory-hogging task as fast as possible,
	 * so as to prevent a system-freeze.
	 */
	if (is_low_mem() == LOWMEM_CRITICAL)
		pagefault_out_of_memory();
#endif

	rcu_read_lock();
	for_each_process(tsk) {
		struct task_struct *p;
		short oom_score_adj;

		if (tsk->flags & PF_KTHREAD)
			continue;

		if (test_task_flag(tsk, TIF_MEMDIE))
			continue;

		p = find_lock_task_mm(tsk);
		if (!p)
			continue;

		oom_score_adj = p->signal->oom_score_adj;
#ifdef CONFIG_ANDROID_PR_KILL
		if (score_adj_check(oom_score_adj) || oom_score_adj < 0) {
#else
		if (oom_score_adj < min_score_adj) {
#endif
			task_unlock(p);
			continue;
		}

		tasksize = get_mm_counter(p->mm, MM_ANONPAGES);
		task_unlock(p);

		if (tasksize <= 0)
			continue;

		if (si == MAX_SWAP_TASKS) {
			sort(&selected[0], MAX_SWAP_TASKS,
					sizeof(struct selected_task),
					&selected_cmp, NULL);
			if (tasksize < selected[0].tasksize)
				continue;
			selected[0].p = p;
			selected[0].oom_score_adj = oom_score_adj;
			selected[0].tasksize = tasksize;
		} else {
			selected[si].p = p;
			selected[si].oom_score_adj = oom_score_adj;
			selected[si].tasksize = tasksize;
			si++;
		}
	}

	for (i = 0; i < si; i++)
		total_sz += selected[i].tasksize;

	/* Skip reclaim if total size is too less */
	if (total_sz < SWAP_CLUSTER_MAX) {
		rcu_read_unlock();
		return;
	}

	for (i = 0; i < si; i++)
		get_task_struct(selected[i].p);

	rcu_read_unlock();

	while (si--) {
#ifdef CONFIG_ANDROID_PR_KILL
		if (is_low_mem() > LOWMEM_NONE &&
			selected[si].oom_score_adj > score_kill_limit) {

			sort_and_kill_tasks(selected, si);
			break;

		} else {
#endif
		nr_to_reclaim =
			(selected[si].tasksize * per_swap_size) / total_sz;
		/* scan atleast a page */
		if (!nr_to_reclaim)
			nr_to_reclaim = 1;

		rp = reclaim_task_anon(selected[si].p, nr_to_reclaim);

#ifndef CONFIG_ANDROID_PR_KILL
		trace_process_reclaim(selected[si].tasksize,
				selected[si].oom_score_adj, rp.nr_scanned,
				rp.nr_reclaimed, per_swap_size, total_sz,
				nr_to_reclaim);
#endif
		total_scan += rp.nr_scanned;
		total_reclaimed += rp.nr_reclaimed;
		put_task_struct(selected[si].p);

#ifdef CONFIG_ANDROID_PR_KILL
		}
#endif
	}

	if (total_scan) {
		efficiency = (total_reclaimed * 100) / total_scan;

		if (efficiency < swap_opt_eff) {
			if (++monitor_eff == swap_eff_win) {
				atomic_set(&skip_reclaim, swap_eff_win);
				monitor_eff = 0;
			}
		} else {
			monitor_eff = 0;
		}

		reclaim_avg_efficiency =
			(efficiency + reclaim_avg_efficiency) / 2;
#ifndef CONFIG_ANDROID_PR_KILL
		trace_process_reclaim_eff(efficiency, reclaim_avg_efficiency);
#endif
	}
}

static int vmpressure_notifier(struct notifier_block *nb,
			unsigned long action, void *data)
{
	unsigned long pressure = action;

#ifndef CONFIG_ANDROID_PR_KILL
	if (!enable_process_reclaim)
		return 0;
#endif

	if (!current_is_kswapd())
		return 0;

	if (atomic_dec_if_positive(&skip_reclaim) >= 0)
		return 0;

	if ((pressure >= pressure_min) && (pressure < pressure_max))
		if (!work_pending(&swap_work))
			queue_work(system_unbound_wq, &swap_work);
	return 0;
}

static struct notifier_block vmpr_nb = {
	.notifier_call = vmpressure_notifier,
};


#ifdef CONFIG_ANDROID_PR_KILL
/*
 * Needed to prevent Android from thinking there's no LMK and thus rebooting.
 * Taken from Simple LMK (@kerneltoast).
 */
static int process_reclaim_init(const char *val, const struct kernel_param *kp)
{
	static atomic_t init_done = ATOMIC_INIT(0);

	if (!atomic_cmpxchg(&init_done, 0, 1)) {
		BUG_ON(vmpressure_notifier_register(&vmpr_nb));
	}

	return 0;
}

static const struct kernel_param_ops process_reclaim_ops = {
	.set = process_reclaim_init
};

#undef MODULE_PARAM_PREFIX
#define MODULE_PARAM_PREFIX "lowmemorykiller."
module_param_cb(minfree, &process_reclaim_ops, NULL, 0200);

#else /*CONFIG_ANDROID_PR_KILL*/

static int __init process_reclaim_init(void)
{
	vmpressure_notifier_register(&vmpr_nb);
	return 0;
}

static void __exit process_reclaim_exit(void)
{
	vmpressure_notifier_unregister(&vmpr_nb);
}

module_init(process_reclaim_init);
module_exit(process_reclaim_exit);

#endif
