#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION="v7.5"
sed -i 's/5.4.287/5.4.288/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v7.4-stable/v7.5-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

