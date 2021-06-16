#!/bin/sh

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v13.9"
sed -i 's/v13.8_stable-EAS/v13.9_stable-EAS/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/ini_set("rom_version",	"v13.8_stable");/ini_set("rom_version",	"v13.9_stable");/' $KERNELDIR/anykernel3/META-INF/com/google/android/aroma-config

echo "	AGNi Version info loaded."

