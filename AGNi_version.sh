#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v4.9"
sed -i 's/5.4.257/5.4.258/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v4.8-stable/v4.9-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

