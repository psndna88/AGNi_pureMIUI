#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v3.7"
sed -i 's/5.4.248/5.4.249/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v3.6-stable/v3.7-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

