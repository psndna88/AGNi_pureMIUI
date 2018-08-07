#!/bin/sh
export KERNELDIR=`readlink -f .`
. ~/WORKING_DIRECTORY/AGNi_stamp.sh
. ~/WORKING_DIRECTORY/gcc-8.x-uber_aarch64.sh

echo ""
echo " Cross-compiling AGNi pureMIUI-MM kernel ..."
echo ""

cd $KERNELDIR/

if [ ! -f $KERNELDIR/.config ];
then
    make agni_kenzo-miuiMM_defconfig
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

if [ "`grep "AGNI_OREO_SYSMOUNT_MARKER" $KERNELDIR/arch/arm/boot/dts/qcom/kenzo/msm8956-kenzo.dtsi`" ];
	then
	sed -i '/AGNI_OREO_SYSMOUNT_MARKER/d' $KERNELDIR/arch/arm/boot/dts/qcom/kenzo/msm8956-kenzo.dtsi
fi
if [ "`grep "AGNI_OREO_vendorMOUNT_MARKER" $KERNELDIR/arch/arm/boot/dts/qcom/kenzo/msm8956-kenzo.dtsi`" ];
	then
	sed -i '/AGNI_OREO_vendorMOUNT_MARKER/d' $KERNELDIR/arch/arm/boot/dts/qcom/kenzo/msm8956-kenzo.dtsi
fi

make -j4 || exit 1
git checkout $KERNELDIR/arch/arm/boot/dts/qcom/kenzo/msm8956-kenzo.dtsi

rm -rf $KERNELDIR/BUILT_kenzo-miuiMM
mkdir -p $KERNELDIR/BUILT_kenzo-miuiMM/system/lib/modules/pronto

find -name '*.ko' -exec mv -v {} $KERNELDIR/BUILT_kenzo-miuiMM/ \;
mv $KERNELDIR/BUILT_kenzo-miuiMM/wlan.ko $KERNELDIR/BUILT_kenzo-miuiMM/system/lib/modules/pronto/pronto_wlan.ko

mv $KERNELDIR/arch/arm64/boot/Image.*-dtb $KERNELDIR/BUILT_kenzo-miuiMM/

echo ""
echo "AGNi pureMIUI-MM has been built for kenzo !!!"

