#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v5.5"
sed -i 's/5.4.269/5.4.271/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v5.4-stable/v5.5-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

