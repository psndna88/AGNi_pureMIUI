#!/bin/sh
export KERNELDIR=`readlink -f .`

COUT="/mnt/Storage-VM/COMPILED_OUT"

if [ -f $COUT/.config ];
	then
	rm $COUT/.config
	rm $COUT/.config.old
	echo "   Compile folder configs cleared !"
else
	echo "   Compile folder has no configs."
fi

