#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v2.9"
sed -i 's/5.4.235/5.4.240/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v2.8-stable/v2.9-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

