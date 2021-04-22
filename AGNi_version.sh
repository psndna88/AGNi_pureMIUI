#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="beta1"
export AGNI_VERSION="v9.0"
sed -i 's/v8.9-stable/v9.0-beta1/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

