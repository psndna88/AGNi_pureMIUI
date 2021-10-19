#!/system/bin/sh

# (c) 2019-2021 changes for blu_spark by eng.stk

if ([ "` uname -r | grep AGNi`" ] || [ -f /sys/module/lpm_levels/parameters/agni_present ]); then

# Wait to set proper init values
sleep 30

# Tweak IO performance after boot complete
for device in a b c d e f; do
    echo "mq-deadline" > /sys/block/sd${device}/queue/scheduler
    echo 512 > /sys/block/sd${device}/queue/read_ahead_kb
    echo 128 > /sys/block/sd${device}/queue/nr_requests
done

# Input boost configuration
echo "0:1094400 1:0 2:0 3:0 4:0 5:0 6:0 7:0" > /sys/devices/system/cpu/cpu_boost/input_boost_freq
echo 500 > /sys/devices/system/cpu/cpu_boost/input_boost_ms

# Disable scheduler core_ctl
echo 0 > /sys/devices/system/cpu/cpu0/core_ctl/enable
echo 0 > /sys/devices/system/cpu/cpu4/core_ctl/enable
echo 0 > /sys/devices/system/cpu/cpu7/core_ctl/enable

## Optimisations
#echo 0 > /dev/stune/foreground/schedtune.prefer_idle
#echo 0 > /dev/stune/top-app/schedtune.prefer_idle
#echo 0 > /dev/stune/top-app/schedtune.boost
#echo "off" > /proc/sys/kernel/printk_devkmsg
#echo 0 > /sys/kernel/debug/tracing/options/trace_printk
#echo 0 > /proc/sys/kernel/sched_autogroup_enabled
#echo 0 > /proc/sys/kernel/randomize_va_space
#echo 0 > /sys/kernel/profiling
#echo 12 > /sys/module/rcutree/parameters/rcu_idle_gp_delay
#echo 1800 > /sys/module/rcutree/parameters/rcu_idle_lazy_gp_delay
#echo 1 > /proc/sys/kernel/perf_cpu_time_max_percent
#echo 0 > /proc/sys/vm/compact_unevictable_allowed
#echo 15 > /proc/sys/vm/stat_interval
#echo 0 > /proc/sys/kernel/perf_event_max_contexts_per_stack
#echo "0 0 0 0" > /proc/sys/kernel/printk
#echo "off" > /proc/sys/kernel/printk_devkmsg
#echo 1 > /proc/sys/kernel/sched_conservative_pl
#echo 128 > /proc/sys/kernel/random/write_wakeup_threshold
#echo 64 > /proc/sys/kernel/random/read_wakeup_threshold

echo "AGNi opt completed " >> /dev/kmsg

fi;
