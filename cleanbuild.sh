#!/bin/sh
export KERNELDIR=`readlink -f .`

if [ -f ~/WORKING_DIRECTORY/AGNi_stamp.sh ]; then
	. ~/WORKING_DIRECTORY/AGNi_stamp.sh
fi
if [ ! -d $COMPILEDIR ]; then
	COUT=$KERNELDIR/OUTPUT
	mkdir $COUT
else
	COUT=$COMPILEDIR_ATOLL
fi

echo "`rm -rf $COUT/*`" > /dev/null
if [ -f $COUT/.config ]; then
	echo "`rm -rf $COUT/.*`" > /dev/null
fi
if [ -d $KERNELDIR/READY_ZIP ];
	then
	echo "`rm -rf $KERNELDIR/READY_ZIP/*`" > /dev/null
fi

echo "   Compile folder EMPTY !"

