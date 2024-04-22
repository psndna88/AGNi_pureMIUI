#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v6.0"
sed -i 's/5.4.274/5.4.275/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v5.9-stable/v6.0-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

