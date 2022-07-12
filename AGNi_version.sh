#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="beta5"
export AGNI_VERSION="v14.1"
sed -i 's/v14.1-beta4/v14.1-beta5/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/4.14.287/4.14.288/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

