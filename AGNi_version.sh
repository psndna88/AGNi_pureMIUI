#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v2.8"
sed -i 's/5.4.231/5.4.235/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v2.7-stable/v2.8-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

