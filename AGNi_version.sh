#!/bin/sh

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="RC4"
export AGNI_VERSION="v8.4"
sed -i 's/v8.4_RC3-EAS/v8.4_RC4-EAS/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

