#!/bin/sh
export KERNELDIR=`readlink -f .`
if [ -f ~/WORKING_DIRECTORY/AGNi_stamp.sh ];
	then
	. ~/WORKING_DIRECTORY/AGNi_stamp.sh
fi
if [ -f ~/WORKING_DIRECTORY/gcc-8.x-uber_aarch64.sh ];
	then
	. ~/WORKING_DIRECTORY/gcc-8.x-uber_aarch64.sh
fi

if [ -f /mnt/Storage-VM/COMPILED_OUT/.config ];
	then
	make menuconfig O=/mnt/Storage-VM/COMPILED_OUT ARCH=arm64
else
	exit 1
fi

