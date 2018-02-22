#!/bin/sh
export KERNELDIR=`readlink -f .`
. ~/WORKING_DIRECTORY/AGNi_stamp.sh
. ~/WORKING_DIRECTORY/gcc-8.x-uber_aarch64.sh

echo ""
echo " Cross-compiling AGNi pureLOS-N kernel ..."
echo ""

cd $KERNELDIR/

if [ ! -f $KERNELDIR/.config ];
then
    make agni_kenzo-losN_defconfig
fi

rm $KERNELDIR/arch/arm/boot/dts/*.dtb
rm $KERNELDIR/drivers/staging/prima/wlan.ko
rm $KERNELDIR/include/generated/compile.h
make -j4 || exit 1

rm -rf $KERNELDIR/BUILT_kenzo-losN
mkdir -p $KERNELDIR/BUILT_kenzo-losN
#mkdir -p $KERNELDIR/BUILT_kenzo-losNwm/system/lib/modules/

find -name '*.ko' -exec mv -v {} $KERNELDIR/BUILT_kenzo-losN/ \;
#find -name '*.ko' -exec mv -v {} $KERNELDIR/BUILT_kenzo-losN/system/lib/modules/ \;

mv $KERNELDIR/arch/arm64/boot/Image.*-dtb $KERNELDIR/BUILT_kenzo-losN/

echo ""
echo "AGNi pureLOS-N has been built for kenzo !!!"

