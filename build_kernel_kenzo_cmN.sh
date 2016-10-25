#!/bin/sh
export KERNELDIR=`readlink -f .`
. ~/AGNi_stamp_CM.sh
. ~/gcc-4.9-uber_aarch64.sh
#. ~/gcc-5.x-uber_aarch64.sh
#. ~/gcc-6.x-uber_aarch64.sh
#. ~/gcc-7.x-uber_aarch64.sh

export ARCH=arm64
mv .git .git-halt

echo ""
echo " Cross-compiling AGNi pureCM-N kernel ..."
echo ""
cd $KERNELDIR/
rm $KERNELDIR/arch/arm64/boot/Image
rm $KERNELDIR/arch/arm64/boot/Image.gz
rm $KERNELDIR/arch/arm64/boot/Image.gz-dtb
rm $KERNELDIR/arch/arm/boot/dts/*.dtb
rm $KERNELDIR/drivers/staging/prima/wlan.ko

if [ ! -f $KERNELDIR/.config ];
then
make agni_kenzo-cmN_defconfig
fi

make -j3 || exit 1

rm -rf $KERNELDIR/BUILT_kenzo-cmN
mkdir -p $KERNELDIR/BUILT_kenzo-cmN

find -name '*.ko' -exec mv -v {} $KERNELDIR/BUILT_kenzo-cmN/ \;
cp $KERNELDIR/arch/arm64/boot/Image.gz-dtb $KERNELDIR/BUILT_kenzo-cmN/

echo ""
echo " BUILT_kenzo-cmN is ready."

mv .git-halt .git

echo "AGNi pureCM-N has been built for kenzo !!!"


