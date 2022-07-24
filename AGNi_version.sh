#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v14.1"
sed -i 's/v14.1-beta5/v14.1-stable/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/4.14.288/4.14.289/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

