#!/bin/bash
export KERNELDIR=`readlink -f .`

CONFIG="agni_haydn_defconfig"

if [ -f ~/WORKING_DIRECTORY/AGNi_stamp.sh ]; then
	. ~/WORKING_DIRECTORY/AGNi_stamp.sh
fi

if [ -f $COMPILEDIR/.config ]; then
	cp -f $COMPILEDIR/.config $KERNELDIR/arch/arm64/configs/$CONFIG
	echo "   configs saved as $CONFIG!"
fi

