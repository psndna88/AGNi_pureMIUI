#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v6.7"
sed -i 's/5.4.279/5.4.281/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v6.6-stable/v6.7-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

