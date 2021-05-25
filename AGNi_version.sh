#!/bin/sh

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v13.6"
sed -i 's/v13.5.1_stable-EAS/v13.6_stable-EAS/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/ini_set("rom_version",	"v13.5.1_stable");/ini_set("rom_version",	"v13.6_stable");/' $KERNELDIR/anykernel3/META-INF/com/google/android/aroma-config

echo "	AGNi Version info loaded."

