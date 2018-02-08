#!/bin/sh
export KERNELDIR=`readlink -f .`
cd $KERNELDIR;

#TEMP_DIR=$KERNELDIR/../TEMP_KENZO_BUILDS_RUN_STORE
#mkdir -p $TEMP_DIR
rm -rf BUILT_kenzo-miuiMM BUILT_kenzo-miuiN BUILT_kenzo-losMM BUILT_kenzo-losN BUILT_kenzo-losO .config .config.old

echo "-----------------------------------------------------------------------"
echo " "
echo "          BATCH MODE: Building AGNi LOS-O variant..."
# AGNI pureLOS-O
rm drivers/staging/prima/wlan.ko
./build_kernel_kenzo_losO.sh
#mv -f $KERNELDIR/BUILT_kenzo-losO $TEMP_DIR/
rm .config
echo " "
echo "          BATCH MODE: Built AGNi LOS-O variant!!!"
echo "-----------------------------------------------------------------------"
echo " "

echo "-----------------------------------------------------------------------"
echo " "
echo "          BATCH MODE: Building AGNi LOS-N variant..."
# AGNI pureLOS-N
rm drivers/staging/prima/wlan.ko
./build_kernel_kenzo_losN.sh
#mv -f $KERNELDIR/BUILT_kenzo-losN $TEMP_DIR/
rm .config
echo " "
echo "          BATCH MODE: Built AGNi LOS-N variant!!!"
echo "-----------------------------------------------------------------------"
echo " "

echo " "
echo "-----------------------------------------------------------------------"
echo " "
echo "          BATCH MODE: Building AGNi MIUI-N variant..."
# AGNI pureMIUI-N
rm drivers/staging/prima/wlan.ko
./build_kernel_kenzo_miuiN.sh
#mv -f $KERNELDIR/BUILT_kenzo-miuiN $TEMP_DIR/
rm .config
echo " "
echo "          BATCH MODE: Built AGNi MIUI-N variant!!!"
echo "-----------------------------------------------------------------------"
echo " "

echo " "
echo "-----------------------------------------------------------------------"
echo " "
echo "          BATCH MODE: Building AGNi MIUI-MM variant..."
# AGNI pureMIUI-MM
rm drivers/staging/prima/wlan.ko
./build_kernel_kenzo_miuiMM.sh
#mv -f $KERNELDIR/BUILT_kenzo-miuiMM $TEMP_DIR/
rm .config
echo " "
echo "          BATCH MODE: Built AGNi MIUI-MM variant !!!"
echo "-----------------------------------------------------------------------"
echo " "

#mv -f $TEMP_DIR/* $KERNELDIR
#rm -rf $TEMP_DIR
echo "          BATCH MODE: Built ALL variants !!!"

