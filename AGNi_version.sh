#!/bin/sh

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v8.4"
sed -i 's/v8.4_RC4-EAS/v8.4_stable-EAS/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

