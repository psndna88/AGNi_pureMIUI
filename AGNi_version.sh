#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v2.5"
sed -i 's/5.4.228/5.4.229/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v2.4-stable/v2.5-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

