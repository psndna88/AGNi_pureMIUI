#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="beta1"
export AGNI_VERSION="v14.1"
sed -i 's/v14.0-stable/v14.1-beta1/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/4.14.284/4.14.286/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

