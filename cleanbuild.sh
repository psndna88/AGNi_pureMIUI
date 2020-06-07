#!/bin/sh
export KERNELDIR=`readlink -f .`

COUT="/mnt/Storage-VM/AGNi_pureMIUI/AGNi_out"

echo "`rm -rf $COUT/*`" > /dev/null
if [ -f $COUT/.config ]; then
	echo "`rm -rf $COUT/.*`" > /dev/null
fi
if [ -d $KERNELDIR/READY_ZIP ];
	then
	echo "`rm -rf $KERNELDIR/READY_ZIP/*`" > /dev/null
fi

echo "   Compile folder EMPTY !"

