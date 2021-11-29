#!/bin/bash
export ARCH=arm64
export SUBARCH=arm64
export BUILDJOBS=4

KERNELDIR=`readlink -f .`

DEVICE="wayne"
CONFIG1="agni_wayne_defconfig"
CONFIG2="agni_wayne-oldcam_defconfig"
CONFIG3="agni_wayne-MIUI-Q_defconfig"
SYNC_CONFIG=1
WLAN_MODA11="$COMPILEDIR/drivers/staging/qcacld-3.0"
WLAN_MODQ="$COMPILEDIR/drivers/staging/qcacld-3.0_Q"
WLAN_MODP="$COMPILEDIR/drivers/staging/qcacld-3.0_pie"
WLAN_MODPO="$COMPILEDIR/drivers/staging/qcacld-3.0_pie_old"
RTL8188EU="$COMPILEDIR/drivers/staging/rtl8188eu"
RTL8712U="$COMPILEDIR/drivers/staging/rtl8712"
RTL8723AU="$COMPILEDIR/drivers/staging/rtl8723au"
RTL8192EU="$COMPILEDIR/drivers/staging/rtl8192eu"

. $KERNELDIR/AGNi_version.sh
FILENAME="AGNi_kernel_$DEVICE-$AGNI_VERSION_PREFIX-$AGNI_VERSION.zip"

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

###### COMPILE new cam
echo ""
echo " ~~~~~ Cross-compiling AGNi kernel $DEVICE ~~~~~"
echo "         VERSION: AGNi $AGNI_VERSION_PREFIX $AGNI_VERSION"
echo ""

rm $COMPILEDIR/.config 2>/dev/null
rm $WLAN_MODA11/*.ko 2>/dev/null
rm $WLAN_MODQ/*.ko 2>/dev/null
rm $WLAN_MODP/*.ko 2>/dev/null
rm $WLAN_MODPO/*.ko 2>/dev/null
rm $RTL8188EU/*.ko 2>/dev/null
rm $RTL8712U/*.ko 2>/dev/null
rm $RTL8723AU/*.ko 2>/dev/null
rm $RTL8192EU/*.ko 2>/dev/null

make defconfig O=$COMPILEDIR $CONFIG1
make -j$BUILDJOBS O=$COMPILEDIR # COMPILE

if [ $SYNC_CONFIG -eq 1 ]; then # SYNC CONFIG
	cp -f $COMPILEDIR/.config $KERNELDIR/arch/arm64/configs/$CONFIG1
fi
rm $COMPILEDIR/.config $COMPILEDIR/.config.old

if [ -f $COMPILEDIR/arch/arm64/boot/Image.gz-dtb ]; then
	mv $COMPILEDIR/arch/arm64/boot/Image.gz-dtb $KERNELDIR/$DIR/Image.gz-dtb-nc
else
	echo "         ERROR: Cross-compiling AGNi (Old Cam) kernel $DEVICE."
	rm -rf $KERNELDIR/$DIR
	exit_reset;
fi	
########## COMPILE new cam END
mv -f $WLAN_MODA11/wlan.ko $KERNELDIR/$DIR/wlan_A11.ko 2>/dev/null
mv -f $WLAN_MODQ/wlan.ko $KERNELDIR/$DIR/wlan_Q.ko 2>/dev/null
mv -f $WLAN_MODP/wlan.ko $KERNELDIR/$DIR/wlan_pie.ko 2>/dev/null
mv -f $WLAN_MODPO/wlan.ko $KERNELDIR/$DIR/wlan_pie_old.ko 2>/dev/null
mv -f $RTL8188EU/8188eu.ko $KERNELDIR/$DIR 2>/dev/null
mv -f $RTL8712U/8712u.ko $KERNELDIR/$DIR 2>/dev/null
mv -f $RTL8723AU/8723au.ko $KERNELDIR/$DIR 2>/dev/null
mv -f $RTL8192EU/8192eu.ko $KERNELDIR/$DIR 2>/dev/null

###### COMPILE old cam
echo ""
echo " ~~~~~ Cross-compiling AGNi kernel (Old Cam) $DEVICE ~~~~~"
echo "         VERSION: AGNi $AGNI_VERSION_PREFIX $AGNI_VERSION"
echo ""

rm $COMPILEDIR/.config 2>/dev/null
rm $WLAN_MODA11/*.ko 2>/dev/null
rm $WLAN_MODQ/*.ko 2>/dev/null
rm $WLAN_MODP/*.ko 2>/dev/null
rm $WLAN_MODPO/*.ko 2>/dev/null
rm $RTL8188EU/*.ko 2>/dev/null
rm $RTL8712U/*.ko 2>/dev/null
rm $RTL8723AU/*.ko 2>/dev/null
rm $RTL8192EU/*.ko 2>/dev/null

make defconfig O=$COMPILEDIR $CONFIG2
make -j$BUILDJOBS O=$COMPILEDIR # COMPILE

if [ $SYNC_CONFIG -eq 1 ]; then # SYNC CONFIG
	cp -f $COMPILEDIR/.config $KERNELDIR/arch/arm64/configs/$CONFIG2
fi
rm $COMPILEDIR/.config $COMPILEDIR/.config.old

if [ -f $COMPILEDIR/arch/arm64/boot/Image.gz-dtb ]; then
	mv $COMPILEDIR/arch/arm64/boot/Image.gz-dtb $KERNELDIR/$DIR/Image.gz-dtb-oc
else
	echo "         ERROR: Cross-compiling AGNi (Old Cam) kernel $DEVICE."
	rm -rf $KERNELDIR/$DIR
	exit_reset;
fi
########## COMPILE old cam END
mv -f $WLAN_MODA11/wlan.ko $KERNELDIR/$DIR/wlan_A11.ko 2>/dev/null
mv -f $WLAN_MODQ/wlan.ko $KERNELDIR/$DIR/wlan_Q.ko 2>/dev/null
mv -f $WLAN_MODP/wlan.ko $KERNELDIR/$DIR/wlan_pie.ko 2>/dev/null
mv -f $WLAN_MODPO/wlan.ko $KERNELDIR/$DIR/wlan_pie_old.ko 2>/dev/null

###### COMPILE MIUI-Q cam
echo ""
echo " ~~~~~ Cross-compiling AGNi kernel (MIUI-Q cam) $DEVICE ~~~~~"
echo "         VERSION: AGNi $AGNI_VERSION_PREFIX $AGNI_VERSION"
echo ""

rm $COMPILEDIR/.config 2>/dev/null
rm $WLAN_MODA11/*.ko 2>/dev/null
rm $WLAN_MODQ/*.ko 2>/dev/null
rm $WLAN_MODP/*.ko 2>/dev/null
rm $WLAN_MODPO/*.ko 2>/dev/null

make defconfig O=$COMPILEDIR $CONFIG3
make -j$BUILDJOBS O=$COMPILEDIR # COMPILE

if [ $SYNC_CONFIG -eq 1 ]; then # SYNC CONFIG
	cp -f $COMPILEDIR/.config $KERNELDIR/arch/arm64/configs/$CONFIG3
fi
rm $COMPILEDIR/.config $COMPILEDIR/.config.old

if [ -f $COMPILEDIR/arch/arm64/boot/Image.gz-dtb ]; then
	mv $COMPILEDIR/arch/arm64/boot/Image.gz-dtb $KERNELDIR/$DIR/Image.gz-dtb-mqc
else
	echo "         ERROR: Cross-compiling AGNi (Old Cam) kernel $DEVICE."
	rm -rf $KERNELDIR/$DIR
	exit_reset;
fi
########## COMPILE MIUI-Q cam END
mv -f $WLAN_MODA11/wlan.ko $KERNELDIR/$DIR/wlan_A11.ko 2>/dev/null
mv -f $WLAN_MODQ/wlan.ko $KERNELDIR/$DIR/wlan_Q.ko 2>/dev/null
mv -f $WLAN_MODP/wlan.ko $KERNELDIR/$DIR/wlan_pie.ko 2>/dev/null
mv -f $WLAN_MODPO/wlan.ko $KERNELDIR/$DIR/wlan_pie_old.ko 2>/dev/null

echo ""

###### ZIP Packing
if ([ -f $KERNELDIR/$DIR/Image.gz-dtb-nc ] && [ -f $KERNELDIR/$DIR/Image.gz-dtb-oc ] && [ -f $KERNELDIR/$DIR/Image.gz-dtb-mqc ]); then
	cp -r $KERNELDIR/anykernel3/* $KERNELDIR/$DIR/
	sed -i 's/device.name1=/device.name1=wayne/' $KERNELDIR/$DIR/anykernel.sh
	sed -i 's/DEVICE_NATIVE_MIUIQ="NO";/DEVICE_NATIVE_MIUIQ="YES";/' $KERNELDIR/$DIR/META-INF/com/google/android/update-binary-installer
	sed -i '/#SDM636/d' $KERNELDIR/$DIR/META-INF/com/google/android/aroma-config
	sed -i '/#SDM636/d' $KERNELDIR/$DIR/META-INF/com/google/android/update-binary-installer
	sed -i '/#SDM636/d' $KERNELDIR/$DIR/tools/ak3-core.sh
	sed -i '/#AGNIFW/d' $KERNELDIR/$DIR/META-INF/com/google/android/aroma-config
	sed -i 's/SETDEVICETYPE/SDM660_wayne (MI 6x)/' $KERNELDIR/$DIR/META-INF/com/google/android/aroma-config
	sed -i 's/SDM660/MI6x/' $KERNELDIR/$DIR/tools/sdm660/init.agni*
	mv -f $KERNELDIR/$DIR/wlan_pie.ko $KERNELDIR/$DIR/tools/wlan_pie.ko 2>/dev/null
	mv -f $KERNELDIR/$DIR/wlan_pie_old.ko $KERNELDIR/$DIR/tools/wlan_pie_old.ko 2>/dev/null
	mv -f $KERNELDIR/$DIR/wlan_Q.ko $KERNELDIR/$DIR/tools/wlan_Q.ko 2>/dev/null
	mv -f $KERNELDIR/$DIR/wlan_A11.ko $KERNELDIR/$DIR/tools/wlan_A11.ko 2>/dev/null
	mv -f $KERNELDIR/$DIR/8*.ko $KERNELDIR/$DIR/tools 2>/dev/null
	rm -rf $KERNELDIR/$DIR/tools/thermals-sdm636
	rm -rf $KERNELDIR/$DIR/tools/sdm636
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
