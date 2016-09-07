/*
 * MSM Hotplug Driver
 *
 * Copyright (c) 2010-2014, The Linux Foundation. All rights reserved.
 * Copyright (c) 2013-2014, Fluxi <linflux@arcor.de>
 * Copyright (c) 2013-2015, Pranav Vashi <neobuddy89@gmail.com>
 * Copyright (c) 2016, jollaman999 <admin@jollaman999.com> / Adaptive for big.LITTLE
 * Copyright (c) 2016, yank555.lu <yank555.lu@gmail.com> / hotplug big core based on load
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

/* ----------------------------------------------------------------------------------------------
   SysFs interface : /sys/module/msm_hotplug
   ----------------------------------------------------------------------------------------------

    msm_enabled          : enable/disable hotplugging
                           (rw, 0/1, defaults to 0)

    update_rate          : delay between load calculations in ms
                           (rw, 2-5000, defaults to 200)

    load_levels          : show/update load levels for little cores
                           (rw, format "x y z"
                              x = 1-4   = core
                              y = 0- (100*x)    = up_threshold
                              z = 0-((100*x)-1) = down_threshold)

    min_cpus_online      : minimum LITTLE cores to keep up at all times
                           (rw, 1-4, defaults to 3 for s810)

    max_cpus_online      : maximum LITTLE cores allowed up at all times when screen on
                           (rw, 1-4, defaults to 4 for s810)

    max_cpus_online_susp : maximum LITTLE cores allowed up at all times when screen off
                           (rw, 1-4, defaults to 4 for s810)

    offline_load         : minimal load to elect a LITTLE core for downing
                           (rw, 0-100, defaults to 0, disabled when 0)

    offline_load_big     : average load of all big cores to reach to remove a big core
                           (rw, 0-100, defaults to 20)

    online_load_big      : average load of all big cores to reach to add an additional big core
                           (rw, 0-100, defaults to 80)

    kick_in_load_big     : average load of all little cores to reach to add first big core
                           (rw, 0-100, defaults to 70)

    fast_lane_load       : average load of all running little cores to reach to bring up all
                           big and LITTLE cores
                           (rw, 0/70-100, defaults to 95, disabled when 0)

    big_core_up_delay    : delay before brining up big cores in ms
                           (rw, defaults to 0, disabled when 0)

    io_is_busy           : consider io as load
                           (rw, 0/1, defaults to 0)

    current_load         : show current load (sum of all up cores' load)
                           (ro)

    current_cores        : show current core up count
                           (ro, format "big.LITTLE")

    version              : show current msm_hotplug version
                           (ro)

   ---------------------------------------------------------------------------------------------- */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/cpufreq.h>
#include <linux/mutex.h>
#include <linux/math64.h>
#include <linux/kernel_stat.h>
#include <linux/fb.h>
#include <linux/tick.h>
#include <linux/hrtimer.h>
#include <asm-generic/cputime.h>
#include <linux/msm_hotplug.h>

#ifdef CONFIG_THERMAL_MONITOR
#include <linux/msm_thermal.h>
#endif

// Change this value for your device
#define LITTLE_CORES    4
#define BIG_CORES       4

#define MSM_HOTPLUG_VERSION             "2.1"

#define MSM_HOTPLUG                     "msm_hotplug"
#define HOTPLUG_ENABLED                 0
#define DEFAULT_UPDATE_RATE             200
#define START_DELAY                     20000
#define DEFAULT_HISTORY_SIZE            10
#define DEFAULT_DOWN_LOCK_DUR           1000
#define DEFAULT_MIN_CPUS_ONLINE         3
#define DEFAULT_MAX_CPUS_ONLINE         LITTLE_CORES
#define DEFAULT_MAX_CPUS_ONLINE_SUSP    2
#define DEFAULT_OFFLINE_LOAD            0
#define DEFAULT_OFFLINE_LOAD_BIG        20
#define DEFAULT_ONLINE_LOAD_BIG         80
#define DEFAULT_KICK_IN_LOAD_BIG        70
#define DEFAULT_FAST_LANE_LOAD          95
#define DEFAULT_BIG_CORE_UP_DELAY       200
#define DEFAULT_IO_IS_BUSY              false

// Use for msm_hotplug_resume_timeout
#define HOTPLUG_TIMEOUT                 2000

unsigned int msm_enabled = HOTPLUG_ENABLED;

struct notifier_block msm_hotplug_fb_notif;

/* HACK: Prevent big cluster turned off when changing governor settings. */
bool prevent_big_off = false;
EXPORT_SYMBOL(prevent_big_off);

static bool timeout_enabled = false;
static s64 pre_time;
bool msm_hotplug_scr_suspended = false;
bool msm_hotplug_fingerprint_called = false;

static void msm_hotplug_suspend(void);

static unsigned int debug = 0;
module_param_named(debug_mask, debug, uint, 0644);

#define dprintk(msg...) \
do {                    \
    if (debug)          \
        pr_info(msg);   \
} while (0)

static struct cpu_hotplug {
    unsigned int suspended;
    unsigned int update_rate;
    unsigned int max_cpus_online_susp;
    unsigned int target_cpus;
    unsigned int target_cpus_big;
    unsigned int min_cpus_online;
    unsigned int max_cpus_online;
    unsigned int offline_load;
    unsigned int offline_load_big;
    unsigned int online_load_big;
    unsigned int kick_in_load_big;
    unsigned int down_lock_dur;
    unsigned int fast_lane_load;
    unsigned int big_core_up_delay;
    struct mutex msm_hotplug_mutex;
    struct notifier_block notif;
} hotplug = {
    .update_rate = DEFAULT_UPDATE_RATE,
    .min_cpus_online = DEFAULT_MIN_CPUS_ONLINE,
    .max_cpus_online = DEFAULT_MAX_CPUS_ONLINE,
    .offline_load = DEFAULT_OFFLINE_LOAD,
    .offline_load_big = DEFAULT_OFFLINE_LOAD_BIG,
    .online_load_big = DEFAULT_ONLINE_LOAD_BIG,
    .kick_in_load_big = DEFAULT_KICK_IN_LOAD_BIG,
    .suspended = 0,
    .max_cpus_online_susp = DEFAULT_MAX_CPUS_ONLINE_SUSP,
    .down_lock_dur = DEFAULT_DOWN_LOCK_DUR,
    .fast_lane_load = DEFAULT_FAST_LANE_LOAD,
    .big_core_up_delay = DEFAULT_BIG_CORE_UP_DELAY
};

static struct workqueue_struct *hotplug_wq;
static struct delayed_work hotplug_work;

static bool big_core_up_ready_checked = false;
static s64 big_core_up_ready_time = 0;

static struct cpu_stats {
    unsigned int *load_hist;
    unsigned int hist_size;
    unsigned int hist_cnt;
    unsigned int min_cpus;
    unsigned int total_cpus;
    unsigned int total_cpus_big;
    unsigned int online_cpus;
    unsigned int online_cpus_big;
    unsigned int cur_avg_load;
    unsigned int cur_avg_load_big;
    unsigned int cur_max_load;
    unsigned int cur_max_load_big;
    struct mutex stats_mutex;
} stats = {
    .hist_size = DEFAULT_HISTORY_SIZE,
    .min_cpus = 1,
    .total_cpus = LITTLE_CORES,
    .total_cpus_big = BIG_CORES
};

struct down_lock {
    unsigned int locked;
    struct delayed_work lock_rem;
};

static DEFINE_PER_CPU(struct down_lock, lock_info);

struct cpu_load_data {
    u64 prev_cpu_idle;
    u64 prev_cpu_wall;
    unsigned int avg_load_maxfreq;
    unsigned int cur_load_maxfreq;
    unsigned int samples;
    unsigned int window_size;
    cpumask_var_t related_cpus;
};

static DEFINE_PER_CPU(struct cpu_load_data, cpuload);

static bool io_is_busy = DEFAULT_IO_IS_BUSY;

static int num_online_little_cpus(void)
{
    int cpu;
    unsigned int online_cpus = 0;

    for (cpu = 0; cpu < stats.total_cpus; cpu++) {
        if (cpu_online(cpu))
            online_cpus++;
    }

    return online_cpus;
}

static int num_online_big_cpus(void)
{
    int cpu;
    unsigned int online_cpus = 0;

    for (cpu = stats.total_cpus; cpu < stats.total_cpus + stats.total_cpus_big; cpu++) {
        if (cpu_online(cpu))
            online_cpus++;
    }

    return online_cpus;
}

static int update_average_load(unsigned int cpu)
{
    int ret;
    unsigned int idle_time, wall_time;
    unsigned int cur_load, load_max_freq;
    u64 cur_wall_time, cur_idle_time;
    struct cpu_load_data *pcpu = &per_cpu(cpuload, cpu);
    struct cpufreq_policy policy;

    ret = cpufreq_get_policy(&policy, cpu);
    if (ret)
        return -EINVAL;

    cur_idle_time = get_cpu_idle_time(cpu, &cur_wall_time, io_is_busy);

    wall_time = (unsigned int) (cur_wall_time - pcpu->prev_cpu_wall);
    pcpu->prev_cpu_wall = cur_wall_time;

    idle_time = (unsigned int) (cur_idle_time - pcpu->prev_cpu_idle);
    pcpu->prev_cpu_idle = cur_idle_time;

    if (unlikely(!wall_time || wall_time < idle_time))
        return 0;

    cur_load = 100 * (wall_time - idle_time) / wall_time;

    /* Calculate the scaled load across cpu */
    load_max_freq = (cur_load * policy.cur) / policy.max;

    if (!pcpu->avg_load_maxfreq) {
        /* This is the first sample in this window */
        pcpu->avg_load_maxfreq = load_max_freq;
        pcpu->window_size = wall_time;
    } else {
        /*
         * The is already a sample available in this window.
         * Compute weighted average with prev entry, so that
         * we get the precise weighted load.
         */
        pcpu->avg_load_maxfreq =
            ((pcpu->avg_load_maxfreq * pcpu->window_size) +
            (load_max_freq * wall_time)) /
            (wall_time + pcpu->window_size);

        pcpu->window_size += wall_time;
    }

    return 0;
}

static unsigned int load_at_max_freq(void)
{
    int cpu;
    unsigned int total_load = 0, max_load = 0;
    struct cpu_load_data *pcpu;

    for (cpu = 0; cpu < stats.total_cpus; cpu++) {
        if (!cpu_online(cpu))
            continue;
        pcpu = &per_cpu(cpuload, cpu);
        update_average_load(cpu);
        total_load += pcpu->avg_load_maxfreq;
        pcpu->cur_load_maxfreq = pcpu->avg_load_maxfreq;
        max_load = max(max_load, pcpu->avg_load_maxfreq);
        pcpu->avg_load_maxfreq = 0;
    }
    stats.cur_max_load = max_load;

    return total_load;
}

static unsigned int load_at_max_freq_big(void)
{
    int cpu;
    unsigned int total_load = 0, max_load = 0;
    struct cpu_load_data *pcpu;

    for (cpu = stats.total_cpus; cpu < stats.total_cpus + stats.total_cpus_big; cpu++) {
        if (!cpu_online(cpu))
            continue;
        pcpu = &per_cpu(cpuload, cpu);
        update_average_load(cpu);
        total_load += pcpu->avg_load_maxfreq;
        pcpu->cur_load_maxfreq = pcpu->avg_load_maxfreq;
        max_load = max(max_load, pcpu->avg_load_maxfreq);
        pcpu->avg_load_maxfreq = 0;
    }
    stats.cur_max_load_big = max_load;

    return total_load;
}

static void update_load_stats(void)
{
    unsigned int i, j;
    unsigned int load = 0;

    mutex_lock(&stats.stats_mutex);
    stats.online_cpus = num_online_little_cpus();
    stats.online_cpus_big = num_online_big_cpus();
    stats.cur_avg_load_big = load_at_max_freq_big();

    if (stats.hist_size > 1) {
        stats.load_hist[stats.hist_cnt] = load_at_max_freq();
    } else {
        stats.cur_avg_load = load_at_max_freq();
        mutex_unlock(&stats.stats_mutex);
        return;
    }

    for (i = 0, j = stats.hist_cnt; i < stats.hist_size; i++, j--) {
        load += stats.load_hist[j];

        if (j == 0)
            j = stats.hist_size;
    }

    if (++stats.hist_cnt == stats.hist_size)
        stats.hist_cnt = 0;

    stats.cur_avg_load = load / stats.hist_size;
    mutex_unlock(&stats.stats_mutex);
}

struct loads_tbl {
    unsigned int up_threshold;
    unsigned int down_threshold;
};

#define LOAD_SCALE(u, d) \
{                        \
    .up_threshold = u,   \
    .down_threshold = d, \
}

static struct loads_tbl loads[] = {
    LOAD_SCALE(400, 0),
    LOAD_SCALE(65, 0),
    LOAD_SCALE(120, 50),
    LOAD_SCALE(190, 100),
    LOAD_SCALE(410, 170),
    LOAD_SCALE(0, 0),
};

static void apply_down_lock(unsigned int cpu)
{
    struct down_lock *dl = &per_cpu(lock_info, cpu);

    dl->locked = 1;
    queue_delayed_work_on(0, hotplug_wq, &dl->lock_rem,
                  msecs_to_jiffies(hotplug.down_lock_dur));
}

static void remove_down_lock(struct work_struct *work)
{
    struct down_lock *dl = container_of(work, struct down_lock,
                        lock_rem.work);
    dl->locked = 0;
}

static int check_down_lock(unsigned int cpu)
{
    struct down_lock *dl = &per_cpu(lock_info, cpu);

    return dl->locked;
}

static int get_lowest_load_cpu(void)
{
    int cpu, lowest_cpu = 0;
    unsigned int lowest_load = UINT_MAX;
    unsigned int cpu_load[stats.total_cpus];
    unsigned int proj_load;
    struct cpu_load_data *pcpu;

    // Skip cpu 0
    for (cpu = 1; cpu < stats.total_cpus; cpu++) {
        if (!cpu_online(cpu))
            continue;
        pcpu = &per_cpu(cpuload, cpu);
        cpu_load[cpu] = pcpu->cur_load_maxfreq;
        if (cpu_load[cpu] < lowest_load) {
            lowest_load = cpu_load[cpu];
            lowest_cpu = cpu;
        }
    }

    proj_load = stats.cur_avg_load - lowest_load;
    if (proj_load > loads[stats.online_cpus - 1].up_threshold)
        return -EPERM;

    if (hotplug.offline_load && lowest_load >= hotplug.offline_load)
        return -EPERM;

    return lowest_cpu;
}

static int get_lowest_load_cpu_big(void)
{
    int cpu, lowest_cpu = 4;
    unsigned int lowest_load = UINT_MAX;
    unsigned int cpu_load[stats.total_cpus_big];
    unsigned int proj_load;
    struct cpu_load_data *pcpu;

    // Take down first big core (cpu4) last
    for (cpu = stats.total_cpus + 1; cpu < stats.total_cpus + stats.total_cpus_big; cpu++) {
        if (!cpu_online(cpu))
            continue;
        pcpu = &per_cpu(cpuload, cpu);
        cpu_load[cpu] = pcpu->cur_load_maxfreq;
        if (cpu_load[cpu] < lowest_load) {
            lowest_load = cpu_load[cpu];
            lowest_cpu = cpu;
        }
    }

    // Don't elect any cpu if the resulting load will trigger adding a core
    proj_load = stats.cur_avg_load_big - lowest_load;
    if (proj_load > (stats.online_cpus - 1) * hotplug.online_load_big)
        return -EPERM;

    return lowest_cpu;
}

static void big_up(void)
{
    int cpu;

    if (!big_core_up_ready_checked && hotplug.big_core_up_delay != 0) {
        big_core_up_ready_checked = true;
        big_core_up_ready_time = ktime_to_ms(ktime_get());
        return;
    }

    if (ktime_to_ms(ktime_get()) - big_core_up_ready_time > hotplug.big_core_up_delay
        || hotplug.big_core_up_delay == 0) {
        for (cpu = stats.total_cpus; cpu < stats.total_cpus + stats.total_cpus_big; cpu++) {
            if (cpu_online(cpu))
                continue;
            if (hotplug.target_cpus_big <= num_online_big_cpus())
                break;
#ifdef CONFIG_THERMAL_MONITOR
            // Only up a cpu if thermal control allows it !
            if(!msm_thermal_deny_cpu_up(cpu)) {
#endif
                cpu_up(cpu);
                apply_down_lock(cpu);
#ifdef CONFIG_THERMAL_MONITOR
            }
#endif
        }

        big_core_up_ready_checked = false;
        return;
    }
}

static void big_down(void)
{
    int cpu, lowest_cpu;
    unsigned int target_big_off; // how many big cores to offline

    target_big_off = stats.total_cpus_big - hotplug.target_cpus_big;

    for (cpu = 0; cpu < stats.total_cpus_big; cpu++) {
        lowest_cpu = get_lowest_load_cpu_big();
        if (lowest_cpu >= stats.total_cpus
                && lowest_cpu <= stats.total_cpus + stats.total_cpus_big) {
            /* HACK: Prevent big cluster turned off when changing governor settings. */
            if (prevent_big_off && lowest_cpu == stats.total_cpus) {
                // Turn on first of big cores
                if (!cpu_online(stats.total_cpus))
#ifdef CONFIG_THERMAL_MONITOR
                    // Only up a cpu if thermal control allows it !
                    if(!msm_thermal_deny_cpu_up(stats.total_cpus)) {
#endif
                        cpu_up(stats.total_cpus);
#ifdef CONFIG_THERMAL_MONITOR
                }
#endif
                continue;
            }

            if (!cpu_online(lowest_cpu))
                continue;
            if (check_down_lock(lowest_cpu))
                continue;
            if (target_big_off <= stats.total_cpus_big - num_online_big_cpus())
                break;
            cpu_down(lowest_cpu);
        }
    }
}

static void big_updown(unsigned int fast_lane_req)
{
    unsigned int avg_load_little_cpus, avg_load_big_cpus;

    hotplug.target_cpus_big = stats.online_cpus_big;

    if (stats.online_cpus_big > 0) {
        avg_load_little_cpus = 0;
        avg_load_big_cpus = stats.cur_avg_load_big / stats.online_cpus_big;
    } else {
        // if no big core is up, use average of all little cores as load average for big kick in
        avg_load_little_cpus = stats.cur_avg_load / stats.online_cpus;
        avg_load_big_cpus = 0;
    }

    if (fast_lane_req == 1) {
        // If fast lane has been requested, up everything we have
        hotplug.target_cpus_big = stats.total_cpus_big;
    } else if (stats.online_cpus == stats.total_cpus) {
        // Only if all little are up, up big cores depending on load, one at a time
        if ((avg_load_little_cpus >= hotplug.kick_in_load_big && stats.online_cpus_big == 0) ||
            (avg_load_big_cpus >= hotplug.online_load_big
                 && stats.online_cpus_big < stats.total_cpus_big)  ) {
            // if the average load of big cluster is above threshold,
            // up one more big core if there is one left to up
            hotplug.target_cpus_big++;
        } else if (avg_load_big_cpus <= hotplug.offline_load_big && stats.online_cpus_big > 0) {
            // if the average load of big cluster is below threshold,
            // down one more big core if there is one left to down
            hotplug.target_cpus_big--;
        }
    } else {
        // If not all little cores are up, big cores can only be down'ed
        if (avg_load_big_cpus <= hotplug.offline_load_big && stats.online_cpus_big > 0) {
            hotplug.target_cpus_big--;
        }
    }

    if (stats.online_cpus_big != hotplug.target_cpus_big) {
        if (hotplug.target_cpus_big > stats.online_cpus_big)
            big_up();
        else if (hotplug.target_cpus_big < stats.online_cpus_big)
            big_down();
    }
}

static void little_up(void)
{
    int cpu;

    // Skip cpu 0
    for (cpu = 1; cpu < stats.total_cpus; cpu++) {
        if (cpu_online(cpu))
            continue;
        if (hotplug.target_cpus <= num_online_little_cpus())
            break;
#ifdef CONFIG_THERMAL_MONITOR
        // Only up a cpu if thermal control allows it !
        if(!msm_thermal_deny_cpu_up(cpu)) {
#endif
            cpu_up(cpu);
            apply_down_lock(cpu);
#ifdef CONFIG_THERMAL_MONITOR
        }
#endif
    }
}

static void little_down(void)
{
    int cpu, lowest_cpu;

    // Skip cpu 0
    for (cpu = 1; cpu < stats.total_cpus; cpu++) {
        lowest_cpu = get_lowest_load_cpu();
        if (!cpu_online(lowest_cpu))
            continue;
        if (lowest_cpu > 0 && lowest_cpu <= stats.total_cpus) {
            if (check_down_lock(lowest_cpu))
                break;
            cpu_down(lowest_cpu);
        }
        if (hotplug.target_cpus >= num_online_little_cpus())
            break;
    }
}

static void online_cpu(unsigned int target)
{
    unsigned int online_little;

    online_little = num_online_little_cpus();

    /* 
     * Do not online more CPUs if max_cpus_online reached 
     * and cancel online task if target already achieved.
     */
    if (target <= online_little ||
        online_little >= hotplug.max_cpus_online)
        return;

    hotplug.target_cpus = target;
    little_up();
}

static void offline_cpu(unsigned int target)
{
    unsigned int online_little;

    online_little = num_online_little_cpus();

    /* 
     * Do not offline more CPUs if min_cpus_online reached
     * and cancel offline task if target already achieved.
     */
    if (target >= online_little ||
        online_little <= hotplug.min_cpus_online)
        return;

    hotplug.target_cpus = target;
    little_down();
}

static void reschedule_hotplug_work(void)
{
    queue_delayed_work_on(0, hotplug_wq, &hotplug_work,
                  msecs_to_jiffies(hotplug.update_rate));
}

static void msm_hotplug_work(struct work_struct *work)
{
    unsigned int i, target = 0, online_little;

    if (!msm_enabled)
        return;

    if (hotplug.suspended) {
        dprintk("%s: suspended.\n", MSM_HOTPLUG);
        return;
    }

    /* HACK: Prevent big cluster turned off when changing governor settings. */
    // Turn on first of big cores
    if (prevent_big_off) {
        if (!cpu_online(stats.total_cpus))
#ifdef CONFIG_THERMAL_MONITOR
            // Only up a cpu if thermal control allows it !
            if(!msm_thermal_deny_cpu_up(stats.total_cpus)) {
#endif
                cpu_up(stats.total_cpus);
#ifdef CONFIG_THERMAL_MONITOR
            }
#endif
    }

    if (timeout_enabled) {
        if (ktime_to_ms(ktime_get()) - pre_time > HOTPLUG_TIMEOUT) {
            if (msm_hotplug_scr_suspended) {
                msm_hotplug_suspend();
                return;
            }

            timeout_enabled = false;
            msm_hotplug_fingerprint_called = false;
        }
        goto reschedule;
    }

    update_load_stats();

    online_little = num_online_little_cpus();

    if (stats.cur_max_load >= (hotplug.fast_lane_load * online_little)
        && hotplug.fast_lane_load != 0) {
        /* Enter the fast lane (disabled when 0)
           this should always be kept high, so only when a big load drops
           we rush into full power */
        online_cpu(hotplug.max_cpus_online);
        big_updown(1);
        goto reschedule;
    }

    /* If number of cpus locked, break out early */
    if (hotplug.min_cpus_online == stats.total_cpus) {
        if (stats.online_cpus != hotplug.min_cpus_online)
            online_cpu(hotplug.min_cpus_online);
        goto reschedule;
    } else if (hotplug.max_cpus_online == stats.min_cpus) {
        if (stats.online_cpus != hotplug.max_cpus_online)
            offline_cpu(hotplug.max_cpus_online);
        goto reschedule;
    }

    for (i = stats.min_cpus; loads[i].up_threshold; i++) {
        if (stats.cur_avg_load <= loads[i].up_threshold
            && stats.cur_avg_load > loads[i].down_threshold) {
            target = i;
            break;
        }
    }

    if (target > hotplug.max_cpus_online)
        target = hotplug.max_cpus_online;
    else if (target < hotplug.min_cpus_online)
        target = hotplug.min_cpus_online;

    if (stats.online_cpus != target) {
        if (target > stats.online_cpus)
            online_cpu(target);
        else if (target < stats.online_cpus)
            offline_cpu(target);
    }
    big_updown(0);

reschedule:

    dprintk("%s: cur_avg_load: %3u online_cpus: %u target: %u\n", MSM_HOTPLUG,
        stats.cur_avg_load, stats.online_cpus, target);
    reschedule_hotplug_work();
}

static void msm_hotplug_suspend(void)
{
    int cpu;

    mutex_lock(&hotplug.msm_hotplug_mutex);
    hotplug.suspended = 1;
    mutex_unlock(&hotplug.msm_hotplug_mutex);

    /* Flush hotplug workqueue */
    if (timeout_enabled) {
        timeout_enabled = false;
        msm_hotplug_fingerprint_called = false;
    } else {
        flush_workqueue(hotplug_wq);
        cancel_delayed_work_sync(&hotplug_work);
    }

    // Turn off little cores but remain max_cpus_online_susp
    // Skip cpu 0
    for (cpu = 1; cpu < stats.total_cpus; cpu++) {
        if (hotplug.max_cpus_online_susp == num_online_little_cpus())
            break;
        cpu_down(cpu);
    }

    // Turn off all of big cores
    for (cpu = stats.total_cpus; cpu < stats.total_cpus + stats.total_cpus_big; cpu++)
        cpu_down(cpu);

    pr_info("%s: suspended.\n", MSM_HOTPLUG);

    return;
}

static void msm_hotplug_resume(void)
{
    int cpu, required_reschedule = 0, required_wakeup = 0;

    if (hotplug.suspended) {
        mutex_lock(&hotplug.msm_hotplug_mutex);
        hotplug.suspended = 0;
        mutex_unlock(&hotplug.msm_hotplug_mutex);
        required_wakeup = 1;
        /* Initiate hotplug work if it was cancelled */
        required_reschedule = 1;
        INIT_DELAYED_WORK(&hotplug_work, msm_hotplug_work);
    }

    if (required_wakeup) {
        /* Fire up all CPUs */
        for_each_cpu_not(cpu, cpu_online_mask) {
            if (cpu == 0)
                continue;
#ifdef CONFIG_THERMAL_MONITOR
            // Only up a cpu if thermal control allows it !
            if(!msm_thermal_deny_cpu_up(cpu)) {
#endif
                cpu_up(cpu);
#ifdef CONFIG_THERMAL_MONITOR
            }
#endif
            if (!timeout_enabled)
                apply_down_lock(cpu);
        }
    }

    /* Resume hotplug workqueue if required */
    if (required_reschedule)
        reschedule_hotplug_work();

    pr_info("%s: resumed.\n", MSM_HOTPLUG);

    return;
}

void msm_hotplug_resume_timeout(void)
{
    if (timeout_enabled || !hotplug.suspended)
        return;

    timeout_enabled = true;
    pre_time = ktime_to_ms(ktime_get());
    msm_hotplug_resume();
}
EXPORT_SYMBOL(msm_hotplug_resume_timeout);

static int msm_hotplug_start(int start_immediately)
{
    int cpu, ret = 0;
    struct down_lock *dl;

    hotplug_wq =
        alloc_workqueue("msm_hotplug_wq", WQ_HIGHPRI | WQ_FREEZABLE, 0);
    if (!hotplug_wq) {
        pr_err("%s: Failed to allocate hotplug workqueue\n",
               MSM_HOTPLUG);
        ret = -ENOMEM;
        goto err_out;
    }

    stats.load_hist = kmalloc(sizeof(stats.hist_size), GFP_KERNEL);
    if (!stats.load_hist) {
        pr_err("%s: Failed to allocate memory\n", MSM_HOTPLUG);
        ret = -ENOMEM;
        goto err_dev;
    }

    mutex_init(&stats.stats_mutex);
    mutex_init(&hotplug.msm_hotplug_mutex);

    INIT_DELAYED_WORK(&hotplug_work, msm_hotplug_work);
    for_each_possible_cpu(cpu) {
        dl = &per_cpu(lock_info, cpu);
        INIT_DELAYED_WORK(&dl->lock_rem, remove_down_lock);
    }

    /* Fire up all CPUs */
    for_each_cpu_not(cpu, cpu_online_mask) {
        if (cpu == 0)
            continue;
#ifdef CONFIG_THERMAL_MONITOR
        // Only up a cpu if thermal control allows it !
        if(!msm_thermal_deny_cpu_up(cpu)) {
#endif
            cpu_up(cpu);
            apply_down_lock(cpu);
#ifdef CONFIG_THERMAL_MONITOR
        }
#endif
    }

    if (start_immediately)
        queue_delayed_work_on(0, hotplug_wq, &hotplug_work, 0);
    else
        queue_delayed_work_on(0, hotplug_wq, &hotplug_work, START_DELAY);

    if (!msm_hotplug_scr_suspended)
        msm_hotplug_resume();
    return ret;
err_dev:
    destroy_workqueue(hotplug_wq);
err_out:
    msm_enabled = 0;
    return ret;
}

static void msm_hotplug_stop(void)
{
    int cpu;
    struct down_lock *dl;

    flush_workqueue(hotplug_wq);
    for_each_possible_cpu(cpu) {
        dl = &per_cpu(lock_info, cpu);
        cancel_delayed_work_sync(&dl->lock_rem);
    }
    cancel_delayed_work_sync(&hotplug_work);

    mutex_destroy(&hotplug.msm_hotplug_mutex);
    mutex_destroy(&stats.stats_mutex);
    kfree(stats.load_hist);

    hotplug.notif.notifier_call = NULL;

    destroy_workqueue(hotplug_wq);

    /* Fire up all CPUs */
    for_each_cpu_not(cpu, cpu_online_mask) {
        if (cpu == 0)
            continue;
#ifdef CONFIG_THERMAL_MONITOR
        // Only up a cpu if thermal control allows it !
        if(!msm_thermal_deny_cpu_up(cpu)) {
#endif
            cpu_up(cpu);
#ifdef CONFIG_THERMAL_MONITOR
        }
#endif
    }
}

/************************** sysfs interface ************************/

static ssize_t show_enable_hotplug(struct device *dev,
                   struct device_attribute *msm_hotplug_attrs,
                   char *buf)
{
    return sprintf(buf, "%u\n", msm_enabled);
}

static ssize_t store_enable_hotplug(struct device *dev,
                    struct device_attribute *msm_hotplug_attrs,
                    const char *buf, size_t count)
{
    int ret;
    unsigned int val;

    ret = sscanf(buf, "%u", &val);
    if (ret != 1)
        return -EINVAL;

    if (val < 0 || val > 1)
        return -EINVAL;

    if (val == msm_enabled)
        return count;

    msm_enabled = val;

    if (msm_enabled)

        msm_hotplug_start(1);

    else
        msm_hotplug_stop();

    return count;
}

static ssize_t show_update_rate(struct device *dev,
                struct device_attribute *msm_hotplug_attrs,
                char *buf)
{
    return sprintf(buf, "%u\n", hotplug.update_rate);
}

static ssize_t store_update_rate(struct device *dev,
                 struct device_attribute *msm_hotplug_attrs,
                 const char *buf, size_t count)
{
    int ret;
    unsigned int val;

    ret = sscanf(buf, "%u", &val);
    if (ret != 1)
        return -EINVAL;

    // load calculations : max. 500 per second / min. 1 every 5 second
    if (val < 2 || val > 5000)
        return -EINVAL;

    hotplug.update_rate = val;

    return count;
}

static ssize_t show_load_levels(struct device *dev,
                struct device_attribute *msm_hotplug_attrs,
                char *buf)
{
    int i, len = 0;

    if (!buf)
        return -EINVAL;

    // only little cores
    for (i = 1; loads[i].up_threshold; i++) {
        len += sprintf(buf + len, "%u ", i);
        len += sprintf(buf + len, "%u ", loads[i].up_threshold);
        len += sprintf(buf + len, "%u\n", loads[i].down_threshold);
    }

    return len;
}

static ssize_t store_load_levels(struct device *dev,
                 struct device_attribute *msm_hotplug_attrs,
                 const char *buf, size_t count)
{
    int ret;
    unsigned int val[3];

    ret = sscanf(buf, "%u %u %u", &val[0], &val[1], &val[2]);
    if (ret != ARRAY_SIZE(val))
        return -EINVAL;

    // only little cores
    if (val[0] < 1 || val[0] > stats.total_cpus || val[2] > val[1])
        return -EINVAL;

    loads[val[0]].up_threshold = val[1];
    loads[val[0]].down_threshold = val[2];

    return count;
}

static ssize_t show_min_cpus_online(struct device *dev,
                    struct device_attribute *msm_hotplug_attrs,
                    char *buf)
{
    return sprintf(buf, "%u\n", hotplug.min_cpus_online);
}

static ssize_t store_min_cpus_online(struct device *dev,
                     struct device_attribute *msm_hotplug_attrs,
                     const char *buf, size_t count)
{
    int ret;
    unsigned int val;

    ret = sscanf(buf, "%u", &val);
    if (ret != 1)
        return -EINVAL;

    if (val < 1 || val > stats.total_cpus)
        return -EINVAL;

    if (hotplug.max_cpus_online < val)
        hotplug.max_cpus_online = val;

    hotplug.min_cpus_online = val;

    return count;
}

static ssize_t show_max_cpus_online(struct device *dev,
                    struct device_attribute *msm_hotplug_attrs,
                    char *buf)
{
    return sprintf(buf, "%u\n",hotplug.max_cpus_online);
}

static ssize_t store_max_cpus_online(struct device *dev,
                     struct device_attribute *msm_hotplug_attrs,
                     const char *buf, size_t count)
{
    int ret;
    unsigned int val;

    ret = sscanf(buf, "%u", &val);
    if (ret != 1)
        return -EINVAL;

    if (val < 1 || val > stats.total_cpus)
        return -EINVAL;

    if (hotplug.min_cpus_online > val)
        hotplug.min_cpus_online = val;

    hotplug.max_cpus_online = val;

    return count;
}

static ssize_t show_max_cpus_online_susp(struct device *dev,
                    struct device_attribute *msm_hotplug_attrs,
                    char *buf)
{
    return sprintf(buf, "%u\n",hotplug.max_cpus_online_susp);
}

static ssize_t store_max_cpus_online_susp(struct device *dev,
                     struct device_attribute *msm_hotplug_attrs,
                     const char *buf, size_t count)
{
    int ret;
    unsigned int val;

    ret = sscanf(buf, "%u", &val);
    if (ret != 1)
        return -EINVAL;

    if (val < 1 || val > stats.total_cpus)
        return -EINVAL;

    hotplug.max_cpus_online_susp = val;

    return count;
}

static ssize_t show_offline_load(struct device *dev,
                 struct device_attribute *msm_hotplug_attrs,
                 char *buf)
{
    return sprintf(buf, "%u\n", hotplug.offline_load);
}

static ssize_t store_offline_load(struct device *dev,
                  struct device_attribute *msm_hotplug_attrs,
                  const char *buf, size_t count)
{
    int ret;
    unsigned int val;

    ret = sscanf(buf, "%u", &val);
    if (ret != 1)
        return -EINVAL;

    if (val < 0 || val > 100)
        return -EINVAL;

    hotplug.offline_load = val;

    return count;
}

static ssize_t show_offline_load_big(struct device *dev,
                 struct device_attribute *msm_hotplug_attrs,
                 char *buf)
{
    return sprintf(buf, "%u\n", hotplug.offline_load_big);
}

static ssize_t store_offline_load_big(struct device *dev,
                  struct device_attribute *msm_hotplug_attrs,
                  const char *buf, size_t count)
{
    int ret;
    unsigned int val;

    ret = sscanf(buf, "%u", &val);
    if (ret != 1)
        return -EINVAL;

    if (val < 0 || val > 100)
        return -EINVAL;

    hotplug.offline_load_big = val;

    return count;
}

static ssize_t show_online_load_big(struct device *dev,
                 struct device_attribute *msm_hotplug_attrs,
                 char *buf)
{
    return sprintf(buf, "%u\n", hotplug.online_load_big);
}

static ssize_t store_online_load_big(struct device *dev,
                  struct device_attribute *msm_hotplug_attrs,
                  const char *buf, size_t count)
{
    int ret;
    unsigned int val;

    ret = sscanf(buf, "%u", &val);
    if (ret != 1)
        return -EINVAL;

    if (val < 0 || val > 100)
        return -EINVAL;

    hotplug.online_load_big = val;

    return count;
}

static ssize_t show_kick_in_load_big(struct device *dev,
                 struct device_attribute *msm_hotplug_attrs,
                 char *buf)
{
    return sprintf(buf, "%u\n", hotplug.kick_in_load_big);
}

static ssize_t store_kick_in_load_big(struct device *dev,
                  struct device_attribute *msm_hotplug_attrs,
                  const char *buf, size_t count)
{
    int ret;
    unsigned int val;

    ret = sscanf(buf, "%u", &val);
    if (ret != 1)
        return -EINVAL;

    if (val < 0 || val > 100)
        return -EINVAL;

    hotplug.kick_in_load_big = val;

    return count;
}

static ssize_t show_fast_lane_load(struct device *dev,
                   struct device_attribute *msm_hotplug_attrs,
                   char *buf)
{
    return sprintf(buf, "%u\n", hotplug.fast_lane_load);
}

static ssize_t store_fast_lane_load(struct device *dev,
                    struct device_attribute *msm_hotplug_attrs,
                    const char *buf, size_t count)
{
    int ret;
    unsigned int val;

    ret = sscanf(buf, "%u", &val);
    if (ret != 1)
        return -EINVAL;

    if (val != 0 || val < 70 || val > 100)
        return -EINVAL;

    hotplug.fast_lane_load = val;

    return count;
}

static ssize_t show_big_core_up_delay(struct device *dev,
                   struct device_attribute *msm_hotplug_attrs,
                   char *buf)
{
    return sprintf(buf, "%u\n", hotplug.big_core_up_delay);
}

static ssize_t store_big_core_up_delay(struct device *dev,
                    struct device_attribute *msm_hotplug_attrs,
                    const char *buf, size_t count)
{
    int ret;
    unsigned int val;

    ret = sscanf(buf, "%u", &val);
    if (ret != 1)
        return -EINVAL;

    hotplug.big_core_up_delay = val;

    return count;
}

static ssize_t show_io_is_busy(struct device *dev,
                   struct device_attribute *msm_hotplug_attrs,
                   char *buf)
{
    return sprintf(buf, "%u\n", io_is_busy);
}

static ssize_t store_io_is_busy(struct device *dev,
                    struct device_attribute *msm_hotplug_attrs,
                    const char *buf, size_t count)
{
    int ret;
    unsigned int val;

    ret = sscanf(buf, "%u", &val);
    if (ret != 1)
        return -EINVAL;

    if (val < 0 || val > 1)
        return -EINVAL;

    io_is_busy = val != 0 ? true : false;

    return count;
}

static ssize_t show_current_load(struct device *dev,
                 struct device_attribute *msm_hotplug_attrs,
                 char *buf)
{
    return sprintf(buf, "%u\n", stats.cur_avg_load);
}

static ssize_t show_current_cores(struct device *dev,
                 struct device_attribute *msm_hotplug_attrs,
                 char *buf)
{
    return sprintf(buf, "big.LITTLE : %u.%u\n", num_online_big_cpus(), num_online_little_cpus());
}

static ssize_t show_version(struct device *dev,
                 struct device_attribute *msm_hotplug_attrs,
                 char *buf)
{
    return sprintf(buf, "%s\n", MSM_HOTPLUG_VERSION);
}

static DEVICE_ATTR(msm_enabled, (S_IWUGO|S_IRUGO), show_enable_hotplug, store_enable_hotplug);
static DEVICE_ATTR(update_rate, (S_IWUGO|S_IRUGO), show_update_rate, store_update_rate);
static DEVICE_ATTR(load_levels, (S_IWUGO|S_IRUGO), show_load_levels, store_load_levels);
static DEVICE_ATTR(min_cpus_online, (S_IWUGO|S_IRUGO), show_min_cpus_online,
            store_min_cpus_online);
static DEVICE_ATTR(max_cpus_online, (S_IWUGO|S_IRUGO), show_max_cpus_online,
            store_max_cpus_online);
static DEVICE_ATTR(max_cpus_online_susp, (S_IWUGO|S_IRUGO), show_max_cpus_online_susp,
            store_max_cpus_online_susp);
static DEVICE_ATTR(offline_load, (S_IWUGO|S_IRUGO), show_offline_load, store_offline_load);
static DEVICE_ATTR(offline_load_big, (S_IWUGO|S_IRUGO), show_offline_load_big,
            store_offline_load_big);
static DEVICE_ATTR(online_load_big, (S_IWUGO|S_IRUGO), show_online_load_big,
            store_online_load_big);
static DEVICE_ATTR(kick_in_load_big, (S_IWUGO|S_IRUGO), show_kick_in_load_big,
            store_kick_in_load_big);
static DEVICE_ATTR(fast_lane_load, (S_IWUGO|S_IRUGO), show_fast_lane_load,
            store_fast_lane_load);
static DEVICE_ATTR(big_core_up_delay, (S_IWUGO|S_IRUGO), show_big_core_up_delay,
            store_big_core_up_delay);
static DEVICE_ATTR(io_is_busy, (S_IWUGO|S_IRUGO), show_io_is_busy, store_io_is_busy);
static DEVICE_ATTR(current_load, S_IRUGO, show_current_load, NULL);
static DEVICE_ATTR(current_cores, S_IRUGO, show_current_cores, NULL);
static DEVICE_ATTR(version, S_IRUGO, show_version, NULL);

static struct attribute *msm_hotplug_attrs[] = {
    &dev_attr_msm_enabled.attr,
    &dev_attr_update_rate.attr,
    &dev_attr_load_levels.attr,
    &dev_attr_min_cpus_online.attr,
    &dev_attr_max_cpus_online.attr,
    &dev_attr_max_cpus_online_susp.attr,
    &dev_attr_offline_load.attr,
    &dev_attr_offline_load_big.attr,
    &dev_attr_online_load_big.attr,
    &dev_attr_kick_in_load_big.attr,
    &dev_attr_fast_lane_load.attr,
    &dev_attr_big_core_up_delay.attr,
    &dev_attr_io_is_busy.attr,
    &dev_attr_current_load.attr,
    &dev_attr_current_cores.attr,
    &dev_attr_version.attr,
    NULL,
};

static struct attribute_group attr_group = {
    .attrs = msm_hotplug_attrs,
};

/************************** sysfs end ************************/

static int msm_hotplug_probe(struct platform_device *pdev)
{
    int ret = 0;
    struct kobject *module_kobj;

    module_kobj = kset_find_obj(module_kset, MSM_HOTPLUG);
    if (!module_kobj) {
        pr_err("%s: Cannot find kobject for module\n", MSM_HOTPLUG);
        goto err_dev;
    }

    ret = sysfs_create_group(module_kobj, &attr_group);
    if (ret) {
        pr_err("%s: Failed to create sysfs: %d\n", MSM_HOTPLUG, ret);
        goto err_dev;
    }

    if (msm_enabled) {
        ret = msm_hotplug_start(0);
        if (ret != 0)
            goto err_dev;
    }

    return ret;
err_dev:
    module_kobj = NULL;
    return ret;
}

static struct platform_device msm_hotplug_device = {
    .name = MSM_HOTPLUG,
    .id = -1,
};

static int msm_hotplug_remove(struct platform_device *pdev)
{
    if (msm_enabled)
        msm_hotplug_stop();

    return 0;
}

static struct platform_driver msm_hotplug_driver = {
    .probe = msm_hotplug_probe,
    .remove = msm_hotplug_remove,
    .driver = {
        .name = MSM_HOTPLUG,
        .owner = THIS_MODULE,
    },
};

static int msm_hotplug_fb_notifier_callback(struct notifier_block *self,
                unsigned long event, void *data)
{
    struct fb_event *evdata = data;
    int *blank;

    if (!msm_enabled)
        return 0;

    if (event == FB_EVENT_BLANK) {
        blank = evdata->data;

        switch (*blank) {
        case FB_BLANK_UNBLANK:
            msm_hotplug_scr_suspended = false;
            msm_hotplug_resume();
            break;
        case FB_BLANK_POWERDOWN:
            prevent_big_off = false;
            msm_hotplug_scr_suspended = true;
            msm_hotplug_suspend();
            break;
        }
    }

    return 0;
}

struct notifier_block msm_hotplug_fb_notif = {
    .notifier_call = msm_hotplug_fb_notifier_callback,
};

static int __init msm_hotplug_init(void)
{
    int ret = false;

    if (stats.total_cpus + stats.total_cpus_big != NR_CPUS) {
        pr_info("%s: Little cores and big cores are not match with this device: %d\n",
                                        MSM_HOTPLUG, ret);
        return -EPERM;
    }

    ret = fb_register_client(&msm_hotplug_fb_notif);
    if (ret) {
        pr_info("%s: FB register failed: %d\n", MSM_HOTPLUG, ret);
        return ret;
    }

    ret = platform_driver_register(&msm_hotplug_driver);
    if (ret) {
        pr_info("%s: Driver register failed: %d\n", MSM_HOTPLUG, ret);
        return ret;
    }

    ret = platform_device_register(&msm_hotplug_device);
    if (ret) {
        pr_info("%s: Device register failed: %d\n", MSM_HOTPLUG, ret);
        return ret;
    }

    pr_info("%s: Device init\n", MSM_HOTPLUG);

    return ret;
}

static void __exit msm_hotplug_exit(void)
{
    platform_device_unregister(&msm_hotplug_device);
    platform_driver_unregister(&msm_hotplug_driver);
    fb_unregister_client(&msm_hotplug_fb_notif);
}

late_initcall(msm_hotplug_init);
module_exit(msm_hotplug_exit);

MODULE_AUTHOR("yank555.lu <yank555.lu@gmail.com>");
MODULE_DESCRIPTION("MSM Hotplug Driver for big.LITTLE");
MODULE_LICENSE("GPLv2");
