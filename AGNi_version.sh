#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v6.9"
sed -i 's/5.4.282/5.4.283/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v6.8-stable/v6.9-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

