#!/bin/sh

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v15.7"
sed -i 's/v15.6_stable-EAS/v15.7_stable-EAS/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/ini_set("rom_version",	"v15.6_stable");/ini_set("rom_version",	"v15.7_stable");/' $KERNELDIR/anykernel3/META-INF/com/google/android/aroma-config

echo "	AGNi Version info loaded."

