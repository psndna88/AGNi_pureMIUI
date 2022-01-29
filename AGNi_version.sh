#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="beta3"
export AGNI_VERSION="v1.0"
sed -i 's/v1.0-beta2/v1.0-beta3/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

