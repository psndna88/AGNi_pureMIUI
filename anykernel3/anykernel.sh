# AnyKernel3 Ramdisk Mod Script
# osm0sis @ xda-developers

## AnyKernel setup
# begin properties
properties() { '
kernel.string=AGNi Kernel by psndna88
do.devicecheck=1
do.modules=0
do.systemless=0
do.cleanup=1
do.cleanuponabort=1
device.name1=curtana
device.name2=Redmi Note 9s
device.name3=Redmi note 9PRO
device.name4=xiaomi
device.name5=RN9
device.name6=joyeuse
device.name7=miatoll
device.name8=gram
device.name9=excalibur
device.name10=joyeuse_eea
device.name11=Joyeuse_EEA
device.name12=durandal
supported.versions=
supported.patchlevels=
'; } # end properties

# shell variables
block=/dev/block/bootdevice/by-name/boot;
is_slot_device=0;
ramdisk_compression=auto;

## AnyKernel methods (DO NOT CHANGE)
# import patching functions/variables - see for reference
. tools/ak3-core.sh;

## AnyKernel file attributes
# set permissions/ownership for included ramdisk files
set_perm_recursive 0 0 755 644 $ramdisk/*;
set_perm_recursive 0 0 750 750 $ramdisk/init* $ramdisk/sbin;

## AnyKernel install
dump_boot;

write_boot;

# AGNi Support magisk module installation
ui_print ">> Installing AGNi Support Module..";
ASUP=/data/adb/modules/AGNiSupport;
rm -rf $ASUP;
mkdir -p $ASUP;
cp -rf /tmp/anykernel/common/AGNiSupport/* $ASUP;
chmod 755 $ASUP/system.prop;
# AGNi Sound magisk module installation
ui_print ">> Installing AGNi Wired Sound Clarity Module..";
ASND=/data/adb/modules/AGNiSound;
rm -rf $ASND;
mkdir -p $ASND;
cp -rf /tmp/anykernel/common/AGNiSound/* $ASND;
chmod 755 $ASND/system.prop;
# remove other AGNi modules
rm -rf /data/adb/modules/AGNiLMKD;
rm -rf /data/adb/modules/AGNiQCOMSVI;
# end magisk module installation

## end install

