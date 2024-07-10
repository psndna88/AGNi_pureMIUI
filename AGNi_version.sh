#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v6.6"
sed -i 's/5.4.278/5.4.279/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v6.5-stable/v6.6-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

