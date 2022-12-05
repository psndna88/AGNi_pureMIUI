#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v2.0"
sed -i 's/5.4.219/5.4.225/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v2.0-beta3/v2.0-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

