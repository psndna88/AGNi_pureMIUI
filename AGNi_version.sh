#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v2.2"
sed -i 's/5.4.225/5.4.226/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v2.1-stable/v2.2-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

