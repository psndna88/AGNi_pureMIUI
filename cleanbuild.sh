#!/bin/bash
export KERNELDIR=`readlink -f .`

if [ -f ~/WORKING_DIRECTORY/AGNi_stamp.sh ]; then
	. ~/WORKING_DIRECTORY/AGNi_stamp.sh
fi
if [ ! -d $COMPILEDIR_HAYDN ]; then
	COUT=$KERNELDIR/OUTPUT
	mkdir $COUT
else
	COUT=$COMPILEDIR_HAYDN
fi

echo "`rm -rf $COUT/*`" > /dev/null
if [ -f $COUT/.config ]; then
	rm -rf $COUT/.*
	rm -rf $COUT/.config
	rm -rf $COUT/.thinlto-cache
fi

echo "   Compile folder EMPTY !"
