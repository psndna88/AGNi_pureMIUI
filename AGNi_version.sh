#!/bin/sh

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="RC2"
export AGNI_VERSION="v8.4"
sed -i 's/v8.4_RC1-EAS/v8.4_RC2-EAS/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

