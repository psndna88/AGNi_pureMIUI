#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v4.0"
sed -i 's/5.4.250/5.4.251/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v3.9.1-stable/v4.0-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

