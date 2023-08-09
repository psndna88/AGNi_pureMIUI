#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v4.2"
sed -i 's/5.4.251/5.4.252/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v4.1-stable/v4.2-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

