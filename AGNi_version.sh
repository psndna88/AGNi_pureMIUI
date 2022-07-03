#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v1.2"
sed -i 's/5.4.202/5.4.203/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v1.1-stable/v1.2-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

