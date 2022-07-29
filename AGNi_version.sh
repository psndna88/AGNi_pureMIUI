#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v14.2"
sed -i 's/v14.1-stable/v14.2-stable/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/4.14.289/4.14.290/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

