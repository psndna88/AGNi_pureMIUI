#!/bin/sh
export KERNELDIR=`readlink -f .`
. ~/WORKING_DIRECTORY/AGNi_stamp_CM.sh
. ~/WORKING_DIRECTORY/gcc-6.x-uber_aarch64.sh
#. ~/WORKING_DIRECTORY/gcc-7.x-uber_aarch64.sh

echo ""
echo " Cross-compiling AGNi pureCM-MM kernel ..."
echo ""

cd $KERNELDIR/

if [ ! -f $KERNELDIR/.config ];
then
    make agni_kenzo-cmMM_defconfig
fi

mv .git .git-halt
rm $KERNELDIR/arch/arm/boot/dts/*.dtb
rm $KERNELDIR/drivers/staging/prima/wlan.ko
make -j3 || exit 1
mv .git-halt .git

rm -rf $KERNELDIR/BUILT_kenzo-cmMM
mkdir -p $KERNELDIR/BUILT_kenzo-cmMM/system/lib/modules/

find -name '*.ko' -exec mv -v {} $KERNELDIR/BUILT_kenzo-cmMM/system/lib/modules/ \;

mv $KERNELDIR/arch/arm64/boot/Image.gz-dtb $KERNELDIR/BUILT_kenzo-cmMM/

echo ""
echo "AGNi pureCM-MM has been built for kenzo !!!"

