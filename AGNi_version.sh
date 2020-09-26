#!/bin/sh

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v10.0"
sed -i 's/v10.0_beta4-EAS/v10.0_stable-EAS/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/ini_set("rom_version",	"v10.0_beta4");/ini_set("rom_version",	"v10.0_stable");/' $KERNELDIR/anykernel3/META-INF/com/google/android/aroma-config

echo "	AGNi Version info loaded."

