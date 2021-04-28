#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="beta4"
export AGNI_VERSION="v9.0"
sed -i 's/v9.0-beta3/v9.0-beta4/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

