#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v5.6"
sed -i 's/5.4.271/5.4.272/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/v5.5-stable/v5.6-stable/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

