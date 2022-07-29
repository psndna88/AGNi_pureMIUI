#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v1.7"
sed -i 's/5.4.207/5.4.208/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v1.6-stable/v1.7-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

