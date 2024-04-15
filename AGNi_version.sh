#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v5.9"
sed -i 's/5.4.273/5.4.274/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v5.8-stable/v5.9-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

