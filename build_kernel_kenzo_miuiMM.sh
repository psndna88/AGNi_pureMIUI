#!/bin/sh
export KERNELDIR=`readlink -f .`
. ~/AGNi_stamp_MIUI.sh
#. ~/gcc-4.9-uber_aarch64.sh
#. ~/gcc-5.x-uber_aarch64.sh
. ~/gcc-6.x-uber_aarch64.sh
#. ~/gcc-7.x-uber_aarch64.sh

export ARCH=arm64

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

# Goodix X
mv $KERNELDIR/arch/arm64/boot/Image.gz-dtb $KERNELDIR/BUILT_kenzo-miuiMM/

# Goodix V
echo ""
git apply goodix.patch && echo "   Goodix Patch applied ..."
mv .git .git-halt
rm $KERNELDIR/arch/arm/boot/dts/*.dtb
make -j3 || exit 1
mv .git-halt .git
mv $KERNELDIR/arch/arm64/boot/Image.gz-dtb $KERNELDIR/BUILT_kenzo-miuiMM/Image.gz-dtb_goodix
git apply -R goodix.patch && echo "   Goodix Patch Cleaned UP."
rm $KERNELDIR/arch/arm/boot/dts/*.dtb
rm $KERNELDIR/arch/arm64/boot/Image
rm $KERNELDIR/arch/arm64/boot/Image.gz

echo ""
echo "AGNi pureMIUI-MM has been built for kenzo !!!"

