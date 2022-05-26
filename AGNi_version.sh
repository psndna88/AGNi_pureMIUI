#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="beta9"
export AGNI_VERSION="v1.0"
sed -i 's/v1.0-beta8/v1.0-beta9/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

