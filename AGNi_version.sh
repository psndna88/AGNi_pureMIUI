#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v3.0"
sed -i 's/5.4.240/5.4.243/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v2.9-stable/v3.0-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

