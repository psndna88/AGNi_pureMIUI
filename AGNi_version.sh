#!/bin/sh

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v8.9"
sed -i 's/v8.9_RC2-EAS/v8.9_stable-EAS/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/ini_set("rom_version",	"v8.9_RC2");/ini_set("rom_version",	"v8.9_stable");/' $KERNELDIR/anykernel3/META-INF/com/google/android/aroma-config

echo "	AGNi Version info loaded."

