#!/bin/bash
export ARCH=arm64
export SUBARCH=arm64
export BUILDJOBS=4

KERNELDIR=`readlink -f .`

DEVICE="jasmine"
CONFIG1="agni_jasmine_VIPsprout_defconfig"
CONFIG2=""
CONFIG3=""
SYNC_CONFIG=1

. $KERNELDIR/AGNi_version.sh
FILENAME="AGNi_kernel_VIPsprout_$DEVICE-$AGNI_VERSION_PREFIX-$AGNI_VERSION.zip"

# AGNi CCACHE SHIFTING TO SDM660
export CCACHE_SDM660="1"
export CCACHE_MIATOLL_Q="0"
export CCACHE_MIATOLL_R="0"
export CCACHE_HAYDN="0"
. ~/WORKING_DIRECTORY/ccache_shifter.sh

exit_reset() {
	export CCACHE_SDM660="0"
	export CCACHE_MIATOLL_Q="0"
	export CCACHE_MIATOLL_R="0"
	export CCACHE_HAYDN="0"
	. ~/WORKING_DIRECTORY/ccache_shifter.sh
	sync
	exit
}

if [ -f ~/WORKING_DIRECTORY/AGNi_stamp.sh ]; then
	. ~/WORKING_DIRECTORY/AGNi_stamp.sh
fi
if [ -f ~/WORKING_DIRECTORY/snapdragon_llvm.sh ]; then
	. ~/WORKING_DIRECTORY/snapdragon_llvm.sh
fi

if [ ! -d $COMPILEDIR ]; then
	COMPILEDIR=$KERNELDIR/OUTPUT
fi
mkdir -p $COMPILEDIR
if [ -d $COMPILEDIR/arch/arm64/boot ]; then
	rm -rf $COMPILEDIR/arch/arm64/boot
fi

if [ -d $BUILT_EXPORT ]; then
	READY_ZIP="$BUILT_EXPORT"
else
	READY_ZIP="$KERNELDIR/READY_ZIP"
fi;
mkdir -p $READY_ZIP 2>/dev/null;
if [ -f $READY_ZIP/$FILENAME ]; then
	rm $READY_ZIP/$FILENAME
fi

DIR="BUILT-$DEVICE"
rm -rf $KERNELDIR/$DIR
mkdir -p $KERNELDIR/$DIR
cd $KERNELDIR/

###### COMPILE 
echo ""
echo " ~~~~~ Cross-compiling AGNi kernel VIPsprout $DEVICE ~~~~~"
echo "         VERSION: AGNi $AGNI_VERSION_PREFIX $AGNI_VERSION"
echo ""

rm $COMPILEDIR/.config 2>/dev/null

make defconfig O=$COMPILEDIR $CONFIG1
make -j$BUILDJOBS O=$COMPILEDIR

if [ $SYNC_CONFIG -eq 1 ]; then # SYNC CONFIG
	cp -f $COMPILEDIR/.config $KERNELDIR/arch/arm64/configs/$CONFIG1
fi
rm $COMPILEDIR/.config $COMPILEDIR/.config.old

if [ -f $COMPILEDIR/arch/arm64/boot/Image.gz-dtb ]; then
	mv $COMPILEDIR/arch/arm64/boot/Image.gz-dtb $KERNELDIR/$DIR/Image.gz-dtb-nc
else
	echo "         ERROR: Cross-compiling AGNi kernel VIPsprout $DEVICE."
	rm -rf $KERNELDIR/$DIR
	exit_reset;
fi
########## COMPILE END

echo ""

###### ZIP Packing
if [ -f $KERNELDIR/$DIR/Image.gz-dtb-nc ]; then
	cp -r $KERNELDIR/anykernel3_VIPsprout/* $KERNELDIR/$DIR/
	sed -i 's/device.name1=/device.name2=jasmine/' $KERNELDIR/$DIR/anykernel.sh
	sed -i 's/device.name2=/device.name1=jasmine_sprout/' $KERNELDIR/$DIR/anykernel.sh
	sed -i 's/DEVICE_NATIVE_MIUIQ="NO";/DEVICE_NATIVE_MIUIQ="YES";/' $KERNELDIR/$DIR/META-INF/com/google/android/update-binary-installer
	sed -i '/#SDM636/d' $KERNELDIR/$DIR/META-INF/com/google/android/aroma-config
	sed -i '/#SDM636/d' $KERNELDIR/$DIR/META-INF/com/google/android/update-binary-installer
	sed -i '/#SDM636/d' $KERNELDIR/$DIR/tools/ak3-core.sh
	sed -i '/#AGNIFW/d' $KERNELDIR/$DIR/META-INF/com/google/android/aroma-config
	sed -i 's/SETDEVICETYPE/SDM660_jasmine (MI A2)/' $KERNELDIR/$DIR/META-INF/com/google/android/aroma-config
	sed -i 's/SDM660/MIA2/' $KERNELDIR/$DIR/tools/sdm660/init.agni*
	cp -f $KERNELDIR/$DIR/tools/sdm660/* $KERNELDIR/$DIR/tools && rm -rf $KERNELDIR/$DIR/tools/sdm660
	cd $KERNELDIR/$DIR/
	zip -rq $READY_ZIP/$FILENAME *
	if [ -f ~/WORKING_DIRECTORY/zipsigner-3.0.jar ]; then
		echo "  Zip Signing...."
		java -jar ~/WORKING_DIRECTORY/zipsigner-3.0.jar $READY_ZIP/$FILENAME $READY_ZIP/$FILENAME-signed 2>/dev/null
		mv $READY_ZIP/$FILENAME-signed $READY_ZIP/$FILENAME
	fi
	rm -rf $KERNELDIR/$DIR
	echo " <<<<< AGNi has been built for $DEVICE !!! >>>>>>"
	echo "         VERSION: AGNi $AGNI_VERSION_PREFIX $AGNI_VERSION"
	echo "            FILE: $FILENAME"
else
	echo " >>>>> AGNi $DEVICE BUILD ERROR <<<<<"
fi

# AGNi CCACHE RESET
export CCACHE_SDM660="0"
export CCACHE_MIATOLL_Q="0"
export CCACHE_MIATOLL_R="0"
export CCACHE_HAYDN="0"
. ~/WORKING_DIRECTORY/ccache_shifter.sh
