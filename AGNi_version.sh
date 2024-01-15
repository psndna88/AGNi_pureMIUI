#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v5.3"
sed -i 's/5.4.265/5.4.267/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v5.2-stable/v5.3-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

