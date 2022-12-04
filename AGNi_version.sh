#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="beta3"
export AGNI_VERSION="v2.0"
sed -i 's/5.4.200/5.4.210/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v2.0-beta2/v2.0-beta3/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

