#!/bin/sh
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
	rm $COUT/.config
	rm $COUT/.config.old
	echo "   Compile folder configs cleared !"
else
	echo "   Compile folder has no configs."
fi

