#!/bin/sh
export ARCH=arm64
export SUBARCH=arm64

KERNELDIR=`readlink -f .`

DEVICE="MIATOLL"
CONFIG1="agni_atoll_aospQR_defconfig"
export AGNI_BUILD_TYPE="AOSP-QR"
SYNC_CONFIG=1
WLAN_MODA11="$COMPILEDIR_ATOLL/drivers/staging/qcacld-3.0"
WLAN_MODQ="$COMPILEDIR_ATOLL/drivers/staging/qcacld-3.0_Q"

. $KERNELDIR/AGNi_version.sh
FILENAME="AGNi_$DEVICE-$AGNI_VERSION_PREFIX-$AGNI_VERSION-$AGNI_BUILD_TYPE.zip"

if [ -f ~/WORKING_DIRECTORY/AGNi_stamp.sh ]; then
	. ~/WORKING_DIRECTORY/AGNi_stamp.sh
fi
if [ -f ~/WORKING_DIRECTORY/snapdragon_llvm.sh ]; then
	. ~/WORKING_DIRECTORY/snapdragon_llvm.sh
fi

if [ ! -d $COMPILEDIR_ATOLL ]; then
	COMPILEDIR_ATOLL=$KERNELDIR/OUTPUT
fi
mkdir -p $COMPILEDIR_ATOLL
if [ -d $COMPILEDIR_ATOLL/arch/arm64/boot ]; then
	rm -rf $COMPILEDIR_ATOLL/arch/arm64/boot
fi

mkdir -p $KERNELDIR/READY_ZIP
if [ -f $KERNELDIR/READY_ZIP/$FILENAME ]; then
	rm $KERNELDIR/READY_ZIP/$FILENAME
fi

DIR="BUILT-$DEVICE"
rm -rf $KERNELDIR/$DIR
mkdir -p $KERNELDIR/$DIR
cd $KERNELDIR/

echo ""
echo " ~~~~~ Cross-compiling AGNi kernel $DEVICE ~~~~~"
echo "         VERSION: AGNi $AGNI_VERSION_PREFIX $AGNI_VERSION $AGNI_BUILD_TYPE"
echo ""

rm $COMPILEDIR_ATOLL/.config 2>/dev/null
rm $WLAN_MODA11/*.ko 2>/dev/null
rm $WLAN_MODQ/*.ko 2>/dev/null

make defconfig O=$COMPILEDIR_ATOLL $CONFIG1
make -j12 O=$COMPILEDIR_ATOLL

if [ $SYNC_CONFIG -eq 1 ]; then # SYNC CONFIG
	cp -f $COMPILEDIR_ATOLL/.config $KERNELDIR/arch/arm64/configs/$CONFIG1
fi
rm $COMPILEDIR_ATOLL/.config $COMPILEDIR_ATOLL/.config.old

if ([ -f $COMPILEDIR_ATOLL/arch/arm64/boot/Image.gz-dtb ] && [ -f $COMPILEDIR_ATOLL/arch/arm64/boot/dtbo.img ]); then
	mv $COMPILEDIR_ATOLL/arch/arm64/boot/Image.gz-dtb $KERNELDIR/$DIR/Image.gz-dtb
	mv $COMPILEDIR_ATOLL/arch/arm64/boot/dtbo.img $KERNELDIR/$DIR/dtbo.img
else
	echo "         ERROR: Cross-compiling AGNi kernel $DEVICE."
	rm -rf $KERNELDIR/$DIR
	exit;
fi

mv -f $WLAN_MODA11/wlan.ko $KERNELDIR/$DIR/wlan_A11.ko 2>/dev/null
mv -f $WLAN_MODQ/wlan.ko $KERNELDIR/$DIR/wlan_Q.ko 2>/dev/null

echo ""

###### ZIP Packing
if ([ -f $KERNELDIR/$DIR/Image.gz-dtb ] || [ -f $KERNELDIR/$DIR/Image.gz ]); then
	cp -r $KERNELDIR/anykernel3/* $KERNELDIR/$DIR/
	mv $KERNELDIR/$DIR/wlan_Q.ko $KERNELDIR/$DIR/tools/wlan_Q.ko 2>/dev/null
	mv $KERNELDIR/$DIR/wlan_A11.ko $KERNELDIR/$DIR/tools/wlan_A11.ko 2>/dev/null
	cd $KERNELDIR/$DIR/
	zip -rq $KERNELDIR/READY_ZIP/$FILENAME *
	if [ -f ~/WORKING_DIRECTORY/zipsigner-3.0.jar ]; then
		echo "  Zip Signing...."
		java -jar ~/WORKING_DIRECTORY/zipsigner-3.0.jar $KERNELDIR/READY_ZIP/$FILENAME $KERNELDIR/READY_ZIP/$FILENAME-signed 2>/dev/null
		mv $KERNELDIR/READY_ZIP/$FILENAME-signed $KERNELDIR/READY_ZIP/$FILENAME
	fi
	rm -rf $KERNELDIR/$DIR
	echo " <<<<< AGNi has been built for $DEVICE !!! >>>>>>"
	echo "         VERSION: AGNi $AGNI_VERSION_PREFIX $AGNI_VERSION $AGNI_BUILD_TYPE"
	echo "            FILE: $FILENAME"
else
	echo " >>>>> AGNi $DEVICE BUILD ERROR <<<<<"
fi

