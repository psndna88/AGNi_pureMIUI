#!/bin/sh
export KERNELDIR=`readlink -f .`
cd $KERNELDIR;

TEMP_DIR=$KERNELDIR/TEMP_KENZO_BUILDS_RUN_STORE
mkdir -p $TEMP_DIR
rm -rf BUILT_kenzo-miuiMM BUILT_kenzo-miuiN BUILT_kenzo-cmMM BUILT_kenzo-cmN .config .config.old

echo "-----------------------------------------------------------------------"
echo " "
echo "          BATCH MODE: Building AGNi LOS-N-O variant..."
# AGNI pureLOS-N
rm drivers/staging/prima/wlan.ko
./build_kernel_kenzo_cmN.sh
mv -f $KERNELDIR/BUILT_kenzo-cmN $TEMP_DIR/
rm .config
echo " "
echo "          BATCH MODE: Built AGNi LOS-N-O variant!!!"
echo "-----------------------------------------------------------------------"
echo " "

echo " "
echo "-----------------------------------------------------------------------"
echo " "
echo "          BATCH MODE: Building AGNi MIUI-N variant..."
# AGNI pureMIUI-N
rm drivers/staging/prima/wlan.ko
./build_kernel_kenzo_miuiN.sh
mv -f $KERNELDIR/BUILT_kenzo-miuiN $TEMP_DIR/
rm .config
echo " "
echo "          BATCH MODE: Built AGNi MIUI-N variant!!!"
echo "-----------------------------------------------------------------------"
echo " "

echo " "
echo "-----------------------------------------------------------------------"
echo " "
echo "          BATCH MODE: Building AGNi LOS-MM variant..."
# AGNI pureLOS-MM
rm drivers/staging/prima/wlan.ko
./build_kernel_kenzo_cmMM.sh
mv -f $KERNELDIR/BUILT_kenzo-cmMM $TEMP_DIR/
rm .config
echo " "
echo "          BATCH MODE: Built AGNi LOS-MM variant!!!"
echo "-----------------------------------------------------------------------"
echo " "

echo " "
echo "-----------------------------------------------------------------------"
echo " "
echo "          BATCH MODE: Building AGNi MIUI-MM variant..."
# AGNI pureMIUI-MM
rm drivers/staging/prima/wlan.ko
./build_kernel_kenzo_miuiMM.sh
mv -f $KERNELDIR/BUILT_kenzo-miuiMM $TEMP_DIR/
rm .config
echo " "
echo "          BATCH MODE: Built AGNi MIUI-MM variant !!!"
echo "-----------------------------------------------------------------------"
echo " "

mv -f $TEMP_DIR/* $KERNELDIR
rm -rf $TEMP_DIR
echo "          BATCH MODE: Built ALL variants !!!"

