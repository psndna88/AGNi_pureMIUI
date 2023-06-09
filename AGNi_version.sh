#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v3.3"
sed -i 's/5.4.245/5.4.246/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v3.2-stable/v3.3-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

