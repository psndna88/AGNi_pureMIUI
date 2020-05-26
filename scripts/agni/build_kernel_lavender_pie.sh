#!/bin/sh
export ARCH=arm64
export SUBARCH=arm64

KERNELDIR=`readlink -f .`
COMPILEDIR="/mnt/ANDROID/COMPILED_OUT"

ANDROID="Pie"
DEVICE="lavender"
CONFIG="agni_lavender-pie_defconfig"
SYNC_CONFIG=1

. $KERNELDIR/AGNi_version.sh
FILENAME="AGNi_$ANDROID-$DEVICE-$AGNI_VERSION_PREFIX-$AGNI_VERSION.zip"

if [ -f ~/WORKING_DIRECTORY/AGNi_stamp.sh ];
	then
	. ~/WORKING_DIRECTORY/AGNi_stamp.sh
fi
if [ -f ~/WORKING_DIRECTORY/gcc-8.x-uber_aarch64.sh ];
	then
	. ~/WORKING_DIRECTORY/gcc-8.x-uber_aarch64.sh
fi

mkdir -p $COMPILEDIR
if [ -d $COMPILEDIR/arch/arm64/boot ];
then
	rm -rf $COMPILEDIR/arch/arm64/boot
fi

echo ""
echo " ~~~~~ Cross-compiling AGNi Android $ANDROID kernel $DEVICE ~~~~~"
echo "         VERSION: AGNi $AGNI_VERSION_PREFIX $AGNI_VERSION"
echo ""

cd $KERNELDIR/

if [ ! -f $COMPILEDIR/.config ];
then
    make defconfig O=$COMPILEDIR $CONFIG
fi

# COMPILE
make -j12 O=$COMPILEDIR

# SYNC CONFIG
if [ $SYNC_CONFIG -eq 1 ]; then
	cp -f $COMPILEDIR/.config $KERNELDIR/arch/arm64/configs/$CONFIG
fi

mkdir -p $KERNELDIR/READY_ZIP/$DEVICE
if [ -f $KERNELDIR/READY_ZIP/$DEVICE/$FILENAME ];
then
	rm $KERNELDIR/READY_ZIP/$DEVICE/$FILENAME
fi

echo ""

if [ -f $COMPILEDIR/arch/arm64/boot/Image.gz-dtb ];
then
	DIR="BUILT-$DEVICE-$ANDROID"
	rm -rf $KERNELDIR/$DIR
	mkdir -p $KERNELDIR/$DIR
	mv $COMPILEDIR/arch/arm64/boot/Image.gz-dtb $KERNELDIR/$DIR/
	cp -r $KERNELDIR/anykernel3/* $KERNELDIR/$DIR/
	sed -i 's/device.name1=/device.name1=lavender/' $KERNELDIR/$DIR/anykernel.sh
	cd $KERNELDIR/$DIR/
	zip -rq $KERNELDIR/READY_ZIP/$DEVICE/$FILENAME *
	rm -rf $KERNELDIR/$DIR
	echo " <<<<< AGNi Android $ANDROID has been built for $DEVICE !!! >>>>>>"
	echo "         VERSION: AGNi $AGNI_VERSION_PREFIX $AGNI_VERSION"
	echo "            FILE: $FILENAME"
else
	echo " >>>>> AGNi Android $ANDROID $DEVICE BUILD ERROR <<<<<"
fi

