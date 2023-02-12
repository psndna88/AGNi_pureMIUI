#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v2.7"
sed -i 's/5.4.230/5.4.231/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v2.6-stable/v2.7-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

