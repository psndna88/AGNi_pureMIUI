#!/bin/bash
export KERNELDIR=`readlink -f .`

if [ -f ~/WORKING_DIRECTORY/AGNi_stamp.sh ]; then
	. ~/WORKING_DIRECTORY/AGNi_stamp.sh
fi
if [ ! -d $COMPILEDIR_ATOLL ]; then
	COUT=$KERNELDIR/OUTPUT
	mkdir $COUT
else
	COUT=$COMPILEDIR_ATOLL
fi

if [ -f $COUT/.config ];
	then
	rm $COUT/.config 2>/dev/null
	rm $COUT/.config.old 2>/dev/null
	echo "   Compile folder configs cleared !"
else
	echo "   Compile folder has no configs."
fi
