#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v5.8"
sed -i 's/5.4.272/5.4.273/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v5.7-stable/v5.8-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

