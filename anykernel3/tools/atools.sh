#!/system/bin/sh
# Please don't hardcode /magisk/modname/... ; instead, please use $MODDIR/...
# This will make your scripts compatible even if Magisk change its mount point in the future

if ([ ! "` uname -r | grep AGNi`" ] || [ ! -f /sys/module/lpm_levels/parameters/agni_present ]); then
	rm -rf /data/adb/modules/AGNiEAS;
	rm -rf /data/adb/modules/AGNiSound;
	rm -rf /data/adb/modules/AGNiSupport;
	rm -rf /data/adb/modules/AGNiWIFI;
	rm -rf /data/adb/modules/AGNiCurtanaThermals;
fi;

if ([ -f /data/adb/modules/AGNi_AOSP_DeepSleepHelper/disable ] || [ ! -d /data/adb/modules/AGNi_AOSP_DeepSleepHelper ]); then
	if [ ! -f /data/media/0/.ANXCamera/.statusfixed ]; then
		rm -rf /data/media/0/.ANXCamera
		mkdir -p /data/media/0/.ANXCamera
		touch /data/media/0/.ANXCamera/.statusfixed
	fi
fi

