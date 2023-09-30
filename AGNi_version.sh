#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v4.8"
sed -i 's/5.4.256/5.4.257/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v4.7-stable/v4.8-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

