#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v1.4"
sed -i 's/5.4.203/5.4.204/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v1.3-stable/v1.4-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

