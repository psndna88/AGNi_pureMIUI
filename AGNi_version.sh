#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v14.3"
sed -i 's/v14.2-stable/v14.3-stable/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/4.14.292/4.14.296/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

