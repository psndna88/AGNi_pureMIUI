#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v2.6"
sed -i 's/5.4.229/5.4.230/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v2.5-stable/v2.6-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

