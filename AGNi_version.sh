#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v1.1"
sed -i 's/5.4.200/5.4.202/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v1.0-stable/v1.1-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

