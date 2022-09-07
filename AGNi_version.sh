#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="beta1"
export AGNI_VERSION="v2.0"
sed -i 's/5.4.180/5.4.191/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v1.9-beta3/v2.0-beta1/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

