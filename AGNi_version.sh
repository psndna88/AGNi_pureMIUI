#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="beta2"
export AGNI_VERSION="v14.1"
sed -i 's/v14.1-beta1/v14.1-beta2/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/4.14.286/4.14.287/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

