#!/bin/sh
export ARCH=arm64
export SUBARCH=arm64

KERNELDIR=`readlink -f .`

DEVICE="whyred"
CONFIG1="agni_whyred_defconfig"
CONFIG2="agni_whyred-oldcam_defconfig"
CONFIG3=""
SYNC_CONFIG=1
WLAN_MODQ="$COMPILEDIR/drivers/staging/qcacld-3.0"
WLAN_MODP="$COMPILEDIR/drivers/staging/qcacld-3.0_pie"
WLAN_MODPO="$COMPILEDIR/drivers/staging/qcacld-3.0_pie_old"

. $KERNELDIR/AGNi_version.sh
FILENAME="AGNi_kernel_$DEVICE-$AGNI_VERSION_PREFIX-$AGNI_VERSION.zip"

if [ -f ~/WORKING_DIRECTORY/AGNi_stamp.sh ]; then
	. ~/WORKING_DIRECTORY/AGNi_stamp.sh
fi
if [ -f ~/WORKING_DIRECTORY/gcc-9.x_aarch64.sh ]; then
	. ~/WORKING_DIRECTORY/gcc-9.x_aarch64.sh
fi

if [ ! -d $COMPILEDIR ]; then
	COMPILEDIR=$KERNELDIR/OUTPUT
fi
mkdir -p $COMPILEDIR
if [ -d $COMPILEDIR/arch/arm64/boot ]; then
	rm -rf $COMPILEDIR/arch/arm64/boot
fi

mkdir -p $KERNELDIR/READY_ZIP
if [ -f $KERNELDIR/READY_ZIP/$FILENAME ]; then
	rm $KERNELDIR/READY_ZIP/$FILENAME
fi

DIR="BUILT-$DEVICE"
rm -rf $KERNELDIR/$DIR
mkdir -p $KERNELDIR/$DIR
cd $KERNELDIR/

###### COMPILE new cam
echo ""
echo " ~~~~~ Cross-compiling AGNi kernel $DEVICE ~~~~~"
echo "         VERSION: AGNi $AGNI_VERSION_PREFIX $AGNI_VERSION"
echo ""

rm $COMPILEDIR/.config 2>/dev/null
rm $WLAN_MODP/*.ko 2>/dev/null
rm $WLAN_MODPO/*.ko 2>/dev/null

make defconfig O=$COMPILEDIR $CONFIG1
make -j12 O=$COMPILEDIR

if [ $SYNC_CONFIG -eq 1 ]; then # SYNC CONFIG
	cp -f $COMPILEDIR/.config $KERNELDIR/arch/arm64/configs/$CONFIG1
fi
rm $COMPILEDIR/.config $COMPILEDIR/.config.old

if [ -f $COMPILEDIR/arch/arm64/boot/Image.gz-dtb ]; then
	mv $COMPILEDIR/arch/arm64/boot/Image.gz-dtb $KERNELDIR/$DIR/Image.gz-dtb-nc
else
	echo "         ERROR: Cross-compiling AGNi (Old Cam) kernel $DEVICE."
	rm -rf $KERNELDIR/$DIR
	exit;
fi
########## COMPILE new cam END
mv -f $WLAN_MODP/wlan.ko $KERNELDIR/$DIR/wlan_pie.ko 2>/dev/null
mv -f $WLAN_MODPO/wlan.ko $KERNELDIR/$DIR/wlan_pie_old.ko 2>/dev/null

###### COMPILE old cam
echo ""
echo " ~~~~~ Cross-compiling AGNi (Old Cam) kernel $DEVICE ~~~~~"
echo "         VERSION: AGNi $AGNI_VERSION_PREFIX $AGNI_VERSION"
echo ""

rm $COMPILEDIR/.config 2>/dev/null
rm $WLAN_MODQ/*.ko 2>/dev/null

make defconfig O=$COMPILEDIR $CONFIG2
make -j12 O=$COMPILEDIR

if [ $SYNC_CONFIG -eq 1 ]; then # SYNC CONFIG
	cp -f $COMPILEDIR/.config $KERNELDIR/arch/arm64/configs/$CONFIG2
fi
rm $COMPILEDIR/.config $COMPILEDIR/.config.old

if [ -f $COMPILEDIR/arch/arm64/boot/Image.gz-dtb ]; then
	mv $COMPILEDIR/arch/arm64/boot/Image.gz-dtb $KERNELDIR/$DIR/Image.gz-dtb-oc
else
	echo "         ERROR: Cross-compiling AGNi (Old Cam) kernel $DEVICE."
	rm -rf $KERNELDIR/$DIR
	exit;
fi
########## COMPILE old cam END
mv -f $WLAN_MODQ/wlan.ko $KERNELDIR/$DIR/wlan_q.ko

echo ""

###### ZIP Packing
if ([ -f $KERNELDIR/$DIR/Image.gz-dtb-nc ] && [ -f $KERNELDIR/$DIR/Image.gz-dtb-oc ] && [ -f $KERNELDIR/$DIR/wlan_pie.ko ] && [ -f $KERNELDIR/$DIR/wlan_q.ko ]); then
	cp -r $KERNELDIR/anykernel3/* $KERNELDIR/$DIR/
	sed -i 's/device.name1=/device.name1=whyred/' $KERNELDIR/$DIR/anykernel.sh
	sed -i '/#NATIVEMIUIQ/d' $KERNELDIR/$DIR/META-INF/com/google/android/aroma-config
	sed -i '/#SDM660/d' $KERNELDIR/$DIR/META-INF/com/google/android/aroma-config
	sed -i 's/SETDEVICETYPE/SDM636_whyred (Redmi Note 5 Pro)/' $KERNELDIR/$DIR/META-INF/com/google/android/aroma-config
	mv $KERNELDIR/$DIR/wlan_pie.ko $KERNELDIR/$DIR/tools/wlan_pie.ko
	mv $KERNELDIR/$DIR/wlan_q.ko $KERNELDIR/$DIR/tools/wlan_q.ko
	mv $KERNELDIR/$DIR/tools/wlan_pie_old.ko.module $KERNELDIR/$DIR/tools/wlan_pie_old.ko 2>/dev/null
	cd $KERNELDIR/$DIR/
	zip -rq $KERNELDIR/READY_ZIP/$FILENAME *
	if [ -f ~/WORKING_DIRECTORY/zipsigner-3.0.jar ]; then
		java -jar ~/WORKING_DIRECTORY/zipsigner-3.0.jar $KERNELDIR/READY_ZIP/$FILENAME $KERNELDIR/READY_ZIP/$FILENAME-signed
		mv $KERNELDIR/READY_ZIP/$FILENAME-signed $KERNELDIR/READY_ZIP/$FILENAME
	fi
	rm -rf $KERNELDIR/$DIR
	echo " <<<<< AGNi has been built for $DEVICE !!! >>>>>>"
	echo "         VERSION: AGNi $AGNI_VERSION_PREFIX $AGNI_VERSION"
	echo "            FILE: $FILENAME"
else
	echo " >>>>> AGNi $DEVICE BUILD ERROR <<<<<"
fi

