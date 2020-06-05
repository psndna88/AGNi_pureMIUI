#!/bin/sh

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v8.6"
sed -i 's/v8.6_RC10-EAS/v8.6_stable-EAS/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

