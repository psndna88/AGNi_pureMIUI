#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v10.4"
sed -i 's/v10.3-stable/v10.4-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

