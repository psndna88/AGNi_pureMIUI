#!/system/bin/sh
# Please don't hardcode /magisk/modname/... ; instead, please use $MODDIR/...
# This will make your scripts compatible even if Magisk change its mount point in the future
MODDIR=${0%/*}

# This script will be executed in post-fs-data mode
# More info in the main Magisk thread

# AGNi Wifi Tweaks
if ([ "` uname -r | grep AGNi`" ] || [ -f /sys/module/lpm_levels/parameters/agni_present ]); then
	if [ ! -d /data/adb/modules/AGNiPSNHOME ]; then
		echo 1 > /proc/sys/net/ipv4/tcp_ecn
		echo 16384 > /proc/sys/net/ipv4/tcp_notsent_lowat
		echo "fq_codel" > /proc/sys/net/core/default_qdisc
		echo "veno" > /proc/sys/net/ipv4/tcp_congestion_control
		echo 0 > /proc/sys/net/ipv4/tcp_tw_reuse 
		echo 0 > /proc/sys/net/ipv4/tcp_slow_start_after_idle
		echo 1 > /proc/sys/net/ipv4/tcp_mtu_probing
		echo 1 > /proc/sys/net/ipv4/tcp_sack
		echo 0 > /proc/sys/net/ipv4/tcp_timestamps
		echo 10000 > /proc/sys/net/core/netdev_max_backlog
		echo 16777216 > /proc/sys/net/core/rmem_max
		echo 8388608 > /proc/sys/net/core/wmem_max
		echo "524288 1048576 5505024" > /proc/sys/net/ipv4/tcp_rmem
		echo "262144 524288 4194304" > /proc/sys/net/ipv4/tcp_wmem
		echo 1 > /proc/sys/net/ipv4/tcp_no_metrics_save
		echo 0 > /proc/sys/net/ipv4/tcp_moderate_rcvbuf
		echo 1 > /proc/sys/net/ipv4/tcp_syncookies
	fi;
else
	rm -rf /data/adb/modules/AGNiWIFI
fi;

