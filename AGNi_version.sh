#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v2.4"
sed -i 's/5.4.227/5.4.228/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v2.3-stable/v2.4-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

