#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v5.2"
sed -i 's/5.4.260/5.4.265/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v5.1-stable/v5.2-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

