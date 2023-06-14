#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v3.5"
sed -i 's/5.4.246/5.4.247/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v3.4-stable/v3.5-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

