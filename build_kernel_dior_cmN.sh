#!/bin/sh
export KERNELDIR=`readlink -f .`
. ~/WORKING_DIRECTORY/AGNi_stamp_CM.sh
#. ~/WORKING_DIRECTORY/gcc-4.9-google_arm.sh
. ~/WORKING_DIRECTORY/gcc-4.9-uber_arm-eabi.sh
#. ~/WORKING_DIRECTORY/gcc-6.x-uber_arm-eabi.sh

echo ""
echo " Cross-compiling AGNi dior pureCM-N kernel ..."
echo ""

cd $KERNELDIR/

if [ ! -f $KERNELDIR/.config ];
then
    make agni_dior-cmN_defconfig
fi

#mv .git .git-halt
rm $KERNELDIR/arch/arm/boot/*.dtb
#rm $KERNELDIR/drivers/staging/prima/wlan.ko
make -j3 || exit 1
#mv .git-halt .git

rm -rf $KERNELDIR/BUILT_dior-cmN
mkdir -p $KERNELDIR/BUILT_dior-cmN/


find -name '*.ko' -exec mv -v {} $KERNELDIR/BUILT_dior-cmN/ \;

mv $KERNELDIR/arch/arm/boot/zImage $KERNELDIR/BUILT_dior-cmN/
if [ -f $KERNELDIR/BUILT_dior-cmN/*.ko ];
    then
    mkdir -p $KERNELDIR/BUILT_dior-cmN/system/lib/modules/
    mv $KERNELDIR/BUILT_dior-cmN/*.ko $KERNELDIR/BUILT_dior-cmN/system/lib/modules/
fi

echo ""
echo "Generating Device Tree image (dt.img) ..."
$KERNELDIR/tools/dtbtool/dtbTool -o $KERNELDIR/BUILT_dior-cmN/dt.img -s 2048 -p $KERNELDIR/scripts/dtc/ $KERNELDIR/arch/arm/boot/

echo ""
echo "AGNi pureCM-N has been built for dior !!!"

