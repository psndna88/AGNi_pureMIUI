#!/system/bin/sh
# Please don't hardcode /magisk/modname/... ; instead, please use $MODDIR/...
# This will make your scripts compatible even if Magisk change its mount point in the future
MODDIR=${0%/*}

# This script will be executed in post-fs-data mode
# More info in the main Magisk thread

# AGNi Wifi Tweaks
if ([ "` uname -r | grep AGNi`" ] || [ -f /sys/module/lpm_levels/parameters/agni_present ]); then
	echo 2 > /proc/sys/net/ipv4/tcp_ecn
	echo "fq" > /proc/sys/net/core/default_qdisc
	echo "bbr" > /proc/sys/net/ipv4/tcp_congestion_control
	echo 1 > /proc/sys/net/ipv4/tcp_tw_reuse 
	echo 0 > /proc/sys/net/ipv4/tcp_slow_start_after_idle
	echo 2 > /proc/sys/net/ipv4/tcp_mtu_probing
	echo 1 > /proc/sys/net/ipv4/tcp_sack
	echo 1 > /proc/sys/net/ipv4/tcp_timestamps
	echo 1 > /proc/sys/net/ipv4/tcp_no_metrics_save
	echo 1 > /proc/sys/net/ipv4/tcp_moderate_rcvbuf
	echo 1 > /proc/sys/net/ipv4/tcp_syncookies
	echo 1 > /proc/sys/net/ipv4/tcp_low_latency
else
	rm -rf /data/adb/modules/AGNiWIFI
fi;
