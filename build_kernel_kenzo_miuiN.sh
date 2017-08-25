#!/bin/sh
export KERNELDIR=`readlink -f .`
. ~/WORKING_DIRECTORY/AGNi_stamp_MIUI.sh
. ~/WORKING_DIRECTORY/gcc-6.x-uber_aarch64.sh
#. ~/WORKING_DIRECTORY/gcc-7.x-uber_aarch64.sh

echo ""
echo " Cross-compiling AGNi pureMIUI-N kernel ..."
echo ""

cd $KERNELDIR/

if [ ! -f $KERNELDIR/.config ];
then
    make agni_kenzo-miuiN_defconfig
fi

rm $KERNELDIR/arch/arm/boot/dts/*.dtb
rm $KERNELDIR/drivers/staging/prima/wlan.ko
make -j3 || exit 1

rm -rf $KERNELDIR/BUILT_kenzo-miuiN
mkdir -p $KERNELDIR/BUILT_kenzo-miuiN/
#mkdir -p $KERNELDIR/BUILT_kenzo-miuiN/system/lib/modules/pronto

find -name '*.ko' -exec mv -v {} $KERNELDIR/BUILT_kenzo-miuiN/ \;
#find -name '*.ko' -exec mv -v {} $KERNELDIR/BUILT_kenzo-miuiN/system/lib/modules/ \;
#mv $KERNELDIR/BUILT_kenzo-miuiN/system/lib/modules/wlan.ko $KERNELDIR/BUILT_kenzo-miuiN/system/lib/modules/pronto/pronto_wlan.ko

mv $KERNELDIR/arch/arm64/boot/Image.*-dtb $KERNELDIR/BUILT_kenzo-miuiN/

echo ""
echo "AGNi pureMIUI-N has been built for kenzo !!!"

