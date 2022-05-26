#!/bin/bash
export KERNELDIR=`readlink -f .`
if [ -f ~/WORKING_DIRECTORY/AGNi_stamp.sh ];
	then
	. ~/WORKING_DIRECTORY/AGNi_stamp.sh
fi
if [ -f ~/WORKING_DIRECTORY/gcc-8.x-uber_aarch64.sh ];
	then
	. ~/WORKING_DIRECTORY/gcc-8.x-uber_aarch64.sh
fi

if [ ! -d $COMPILEDIR_HAYDN ]; then
	COUT=$KERNELDIR/OUTPUT
	mkdir $COUT
else
	COUT=$COMPILEDIR_HAYDN
fi

if [ -f $COUT/.config ];
	then
	make menuconfig O=$COUT ARCH=arm64
else
	exit 1
fi

