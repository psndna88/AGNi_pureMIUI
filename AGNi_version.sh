#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v7.3"
sed -i 's/5.4.285/5.4.286/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v7.2-stable/v7.3-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

