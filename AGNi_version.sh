#!/bin/bash

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="beta25"
export AGNI_VERSION="v1.0"
sed -i 's/v1.0-beta24/v1.0-beta25/' $KERNELDIR/arch/arm64/configs/agni_*

echo "	AGNi Version info loaded."

