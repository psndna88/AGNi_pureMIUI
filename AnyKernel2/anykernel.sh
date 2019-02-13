# AnyKernel2 Ramdisk Mod Script
# osm0sis @ xda-developers

## AnyKernel setup
# begin properties
properties() { '
kernel.string=Escrima-X26-Kamui-LightningBlade-PurpleLightning
do.devicecheck=1
do.modules=0
do.cleanup=1
do.cleanuponabort=0
device.name1=kenzo
device.name2=o
device.name3=s
device.name4=kate
device.name5=
'; } # end properties

# shell variables
block=/dev/block/bootdevice/by-name/boot;
is_slot_device=0;


## AnyKernel methods (DO NOT CHANGE)
# import patching functions/variables - see for reference
. /tmp/anykernel/tools/ak2-core.sh;


## AnyKernel install
dump_boot;
write_boot;

## end install

