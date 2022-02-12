#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v12.7"
sed -i 's/4.14.265/4.14.266/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v12.6-stable/v12.7-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

