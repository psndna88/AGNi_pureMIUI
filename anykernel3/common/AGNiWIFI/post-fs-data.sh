#!/system/bin/sh
# Please don't hardcode /magisk/modname/... ; instead, please use $MODDIR/...
# This will make your scripts compatible even if Magisk change its mount point in the future
MODDIR=${0%/*}

# This script will be executed in post-fs-data mode
# More info in the main Magisk thread

# AGNi Wifi Tweaks
if ([ "` uname -r | grep AGNi`" ] || [ -f /sys/module/lpm_levels/parameters/agni_present ]); then
	if [ ! -d /data/adb/modules/AGNiPSNHOME ]; then
		echo 0 > /proc/sys/net/ipv4/tcp_ecn
		echo "fq_codel" > /proc/sys/net/core/default_qdisc
		echo "veno" > /proc/sys/net/ipv4/tcp_congestion_control
		echo 1 > /proc/sys/net/ipv4/tcp_tw_reuse 
		echo 0 > /proc/sys/net/ipv4/tcp_slow_start_after_idle
		echo 2 > /proc/sys/net/ipv4/tcp_mtu_probing
		echo 1 > /proc/sys/net/ipv4/tcp_sack
		echo 1 > /proc/sys/net/ipv4/tcp_timestamps
		#net.core.netdev_max_backlog = 10000
		echo 16777216 > /proc/sys/net/core/rmem_max
		echo 16777216 > /proc/sys/net/core/wmem_max
		echo "4096 87380 16777216" > /proc/sys/net/ipv4/tcp_rmem
		echo "4096 87380 16777216" > /proc/sys/net/ipv4/tcp_wmem
		echo 1 > /proc/sys/net/ipv4/tcp_no_metrics_save
		echo 1 > /proc/sys/net/ipv4/tcp_moderate_rcvbuf
		echo 1 > /proc/sys/net/ipv4/tcp_syncookies
	fi;
else
	rm -rf /data/adb/modules/AGNiWIFI
fi;

