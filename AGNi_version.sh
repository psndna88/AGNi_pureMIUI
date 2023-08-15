#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v4.3"
sed -i 's/5.4.252/5.4.254/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v4.2-stable/v4.3-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

