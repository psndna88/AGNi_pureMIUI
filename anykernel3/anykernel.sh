# AnyKernel3 Ramdisk Mod Script
# osm0sis @ xda-developers

## AnyKernel setup
# begin properties
properties() { '
kernel.string=AGNi Kernel EAS (AVOID 3rd Party Memory tweaks)
do.devicecheck=1
do.modules=0
do.systemless=0
do.cleanup=1
do.cleanuponabort=1
device.name1=
device.name2=
device.name3=
device.name4=
device.name5=
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

patch_cmdline android.ver android.ver=9;
patch_cmdline miui miui=1;
patch_cmdline srgblock srgblock=0;
patch_cmdline cpuoc cpuoc=0;
patch_cmdline agnisoundmod agnisoundmod=0;
patch_cmdline wiredbtnaltmode wiredbtnaltmode=0;
patch_cmdline ledmode ledmode=0;
patch_cmdline losxattr losxattr=0;
patch_cmdline selfake selfake=0;
write_boot;
## end install

