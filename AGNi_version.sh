#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v6.5"
sed -i 's/5.4.277/5.4.278/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v6.4.1-stable/v6.5-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

