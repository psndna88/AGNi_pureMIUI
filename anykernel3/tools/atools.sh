#!/system/bin/sh
# Please don't hardcode /magisk/modname/... ; instead, please use $MODDIR/...
# This will make your scripts compatible even if Magisk change its mount point in the future

if ([ ! "` uname -r | grep AGNi`" ] || [ ! -f /sys/module/lpm_levels/parameters/agni_present ]); then
S	rm -rf /data/adb/modules/AGNiEAS;
	rm -rf /data/adb/modules/AGNiSound;
	rm -rf /data/adb/modules/AGNiSupport;
	rm -rf /data/adb/modules/AGNiWIFI;
	rm -rf /data/adb/modules/AGNi_AOSP_DeepSleepHelper;
else
	## Optimisations
#	echo 0 > /dev/stune/foreground/schedtune.prefer_idle
#	echo 0 > /dev/stune/top-app/schedtune.prefer_idle
#	echo 0 > /dev/stune/top-app/schedtune.boost
#	echo "off" > /proc/sys/kernel/printk_devkmsg
#	echo 0 > /sys/kernel/debug/tracing/options/trace_printk
#	echo 0 > /proc/sys/kernel/sched_autogroup_enabled
#	echo 0 > /proc/sys/kernel/randomize_va_space
#	echo 0 > /sys/kernel/profiling
	#echo 4000 > /sys/devices/system/cpu/cpu0/cpufreq/schedutil/up_rate_limit_us
	#echo 200 > /sys/devices/system/cpu/cpu0/cpufreq/schedutil/down_rate_limit_us
	#echo 12 > /sys/module/rcutree/parameters/rcu_idle_gp_delay
	#echo 1800 > /sys/module/rcutree/parameters/rcu_idle_lazy_gp_delay
	#echo 1 > /proc/sys/kernel/perf_cpu_time_max_percent
#	echo 0 > /proc/sys/vm/compact_unevictable_allowed
	#echo 15 > /proc/sys/vm/stat_interval
	#echo 0 > /proc/sys/kernel/perf_event_max_contexts_per_stack
#	echo "0 0 0 0" > /proc/sys/kernel/printk
#	echo "off" > /proc/sys/kernel/printk_devkmsg
	#echo 1 > /proc/sys/kernel/sched_conservative_pl
#	echo 128 > /proc/sys/kernel/random/write_wakeup_threshold
#	echo 64 > /proc/sys/kernel/random/read_wakeup_threshold
#	echo 0 > /proc/sys/kernel/sched_boost
	#echo 0 > /proc/sys/net/ipv4/tcp_timestamps
	# Stune
	echo 1 > /dev/stune/top-app/schedtune.sched_boost
	echo 40 > /dev/stune/top-app/schedtune.boost
fi;
rm -rf /data/adb/modules/AGNiCurtanaThermals;

if ([ -f /data/adb/modules/AGNi_AOSP_DeepSleepHelper/disable ] || [ ! -d /data/adb/modules/AGNi_AOSP_DeepSleepHelper ]); then
	if [ ! -f /data/media/0/.ANXCamera/.statusfixed ]; then
		rm -rf /data/media/0/.ANXCamera
		mkdir -p /data/media/0/.ANXCamera
		touch /data/media/0/.ANXCamera/.statusfixed
	fi
fi

