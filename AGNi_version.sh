#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v7.0"
sed -i 's/5.4.283/5.4.284/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v6.9-stable/v7.0-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

