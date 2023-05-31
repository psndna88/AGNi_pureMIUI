#!/bin/bash
export KERNELDIR=`readlink -f .`

ABI_DUMP_FILE="abi_gki_aarch64_agni.xml"

if [ -f ~/WORKING_DIRECTORY/AGNi_stamp.sh ]; then
	. ~/WORKING_DIRECTORY/AGNi_stamp.sh
fi

if [ ! -d $COMPILEDIR_HAYDN ]; then
	COUT=$KERNELDIR/OUTPUT
	mkdir $COUT
else
	COUT=$COMPILEDIR_HAYDN
fi

if [ -d $KERNEL_ABI_TOOLS ]; then
	mkdir -p $KERNELDIR/android 2>/dev/null;
	echo "  generating ABI..."
	cd $KERNEL_ABI_TOOLS && ./dump_abi --linux-tree $COUT --out-file $KERNELDIR/android/$ABI_DUMP_FILE;
	echo "  ALL DONE!"
else
	echo "Warning: google ABI TOOLS not found!"
fi

