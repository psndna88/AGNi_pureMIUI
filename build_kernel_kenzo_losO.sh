#!/bin/sh
export KERNELDIR=`readlink -f .`
. ~/WORKING_DIRECTORY/AGNi_stamp.sh
. ~/WORKING_DIRECTORY/gcc-8.x-uber_aarch64.sh

echo ""
echo " Cross-compiling AGNi pureLOS-O kernel ..."
echo ""

cd $KERNELDIR/

if [ ! -f $KERNELDIR/.config ];
then
    make agni_kenzo-losO_defconfig
fi

rm $KERNELDIR/arch/arm/boot/dts/*.dtb
rm $KERNELDIR/drivers/staging/prima/wlan.ko
rm $KERNELDIR/include/generated/compile.h
make -j4 || exit 1

rm -rf $KERNELDIR/BUILT_kenzo-losO
mkdir -p $KERNELDIR/BUILT_kenzo-losO

find -name '*.ko' -exec mv -v {} $KERNELDIR/BUILT_kenzo-losO/ \;
#find -name '*.ko' -exec mv -v {} $KERNELDIR/BUILT_kenzo-losO/system/lib/modules/ \;

mv $KERNELDIR/arch/arm64/boot/Image.*-dtb $KERNELDIR/BUILT_kenzo-losO/

echo ""
echo "AGNi pureLOS-O has been built for kenzo !!!"

