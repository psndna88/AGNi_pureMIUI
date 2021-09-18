#!/system/bin/sh
# Please don't hardcode /magisk/modname/... ; instead, please use $MODDIR/...
# This will make your scripts compatible even if Magisk change its mount point in the future
MODDIR=${0%/*}

# This script will be executed in post-fs-data mode
# More info in the main Magisk thread

if ([ "` uname -r | grep AGNi`" ] || [ -f /sys/module/lpm_levels/parameters/agni_present ]); then
	if [ -d /system/priv-app/MiuiCamera ]; then
		mkdir -p /data/media/0/.MiuiCamera/
		touch /data/media/0/.MiuiCamera/.check
		mkdir -p /data/media/0/MIUI/debug_log
		rm -rf /data/media/0/.ANXCamera
#		stop anxfeatures
	else
	    rm -rf /data/media/0/.ANXCamera
	    mkdir -p /data/media/0/.ANXCamera/cheatcodes
	    cp -R /system/etc/ANXCamera/cheatcodes/* /data/media/0/.ANXCamera/cheatcodes
	    mkdir -p /data/media/0/.ANXCamera/cheatcodes_reference
	    cp -R /system/etc/ANXCamera/cheatcodes/* /data/media/0/.ANXCamera/cheatcodes_reference
	    mkdir -p /data/media/0/.ANXCamera/features
	    cp -R /system/etc/ANXCamera/features/* /data/media/0/.ANXCamera/features
	    mkdir -p /data/media/0/.ANXCamera/features_reference
	    cp -R /system/etc/ANXCamera/features/* /data/media/0/.ANXCamera/features_reference
	    touch /data/media/0/.ANXCamera/.check
	fi
else
	rm -rf /data/adb/modules/AGNi_AOSP_DeepSleepHelper;
fi;

