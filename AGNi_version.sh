#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v1.6"
sed -i 's/5.4.204/5.4.205/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v1.5-stable/v1.6-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

