#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v6.1"
sed -i 's/5.4.275/5.4.277/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v6.0-stable/v6.1-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

