#!/bin/sh
export KERNELDIR=`readlink -f .`
. ~/WORKING_DIRECTORY/AGNi_stamp_MIUI.sh
. ~/WORKING_DIRECTORY/gcc-6.x-uber_aarch64.sh
#. ~/WORKING_DIRECTORY/gcc-7.x-uber_aarch64.sh

echo ""
echo " Cross-compiling AGNi pureMIUI-MM kernel ..."
echo ""

cd $KERNELDIR/

if [ ! -f $KERNELDIR/.config ];
then
    make agni_kenzo-miuiMM_defconfig
fi

mv .git .git-halt
rm $KERNELDIR/arch/arm/boot/dts/*.dtb
rm $KERNELDIR/drivers/staging/prima/wlan.ko
make -j3 || exit 1
mv .git-halt .git

rm -rf $KERNELDIR/BUILT_kenzo-miuiMM
mkdir -p $KERNELDIR/BUILT_kenzo-miuiMM/system/lib/modules/pronto

find -name '*.ko' -exec mv -v {} $KERNELDIR/BUILT_kenzo-miuiMM/system/lib/modules/ \;
mv $KERNELDIR/BUILT_kenzo-miuiMM/system/lib/modules/wlan.ko $KERNELDIR/BUILT_kenzo-miuiMM/system/lib/modules/pronto/pronto_wlan.ko

mv $KERNELDIR/arch/arm64/boot/Image.gz-dtb $KERNELDIR/BUILT_kenzo-miuiMM/

echo ""
echo "AGNi pureMIUI-MM has been built for kenzo !!!"

