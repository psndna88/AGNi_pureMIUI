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

echo "`rm -rf $COUT/*`" 2>/dev/null
if [ -f $COUT/.config ]; then
	rm -rf $COUT/.* 2>/dev/null
	rm -rf $COUT/.config 2>/dev/null
fi

echo "   Compile folder EMPTY !"
