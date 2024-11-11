#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v7.2"
sed -i 's/5.4.284/5.4.285/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v7.1-stable/v7.2-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

