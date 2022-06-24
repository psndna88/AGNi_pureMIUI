#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v1.0"
sed -i 's/5.4.199/5.4.200/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v1.0-beta27/v1.0-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

