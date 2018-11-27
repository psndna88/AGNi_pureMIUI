#!/bin/sh
export KERNELDIR=`readlink -f .`
. ~/WORKING_DIRECTORY/AGNi_stamp.sh
. ~/WORKING_DIRECTORY/gcc-8.x-uber_aarch64.sh

echo ""
echo " Cross-compiling AGNi purePIE treble kernel ..."
echo ""

cd $KERNELDIR/

if [ ! -f $KERNELDIR/.config ];
then
    make agni_kenzo-pie_defconfig
fi

rm $KERNELDIR/arch/arm/boot/dts/*.dtb
if [ -f $KERNELDIR/drivers/staging/prima/wlan.ko ];
	then
	rm $KERNELDIR/drivers/staging/prima/wlan.ko
fi
if [ -f $KERNELDIR/drivers/staging/prima_n/wlan.ko ];
	then
	rm $KERNELDIR/drivers/staging/prima_n/wlan.ko
fi
if [ -f $KERNELDIR/include/generated/compile.h ];
	then
	rm $KERNELDIR/include/generated/compile.h
fi

git checkout $KERNELDIR/arch/arm/boot/dts/qcom/kenzo/msm8956-kenzo.dtsi

make -j4 || exit 1

rm -rf $KERNELDIR/BUILT_kenzo-pie_treble
mkdir -p $KERNELDIR/BUILT_kenzo-pie_treble

find -name '*.ko' -exec mv -v {} $KERNELDIR/BUILT_kenzo-pie_treble/ \;

mv $KERNELDIR/arch/arm64/boot/Image.*-dtb $KERNELDIR/BUILT_kenzo-pie_treble/

echo ""
echo "AGNi purePIE treble has been built for kenzo !!!"

