#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v14.5"
sed -i 's/v14.4-stable/v14.5-stable/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/4.14.296/4.14.305/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

