#!/bin/sh
export KERNELDIR=`readlink -f .`
cd $KERNELDIR;

#TEMP_DIR=$KERNELDIR/../TEMP_KENZO_BUILDS_RUN_STORE
#mkdir -p $TEMP_DIR
rm -rf BUILT_kenzo-miuiMM BUILT_kenzo-miuiN BUILT_kenzo-miuiO BUILT_kenzo-losMM BUILT_kenzo-losN BUILT_kenzo-losO BUILT_kenzo-losO_treble .config .config.old

echo "-----------------------------------------------------------------------"
echo " "
echo "          BATCH MODE: Building AGNi LOS-O treble variant..."
# AGNI pureLOS-O treble
rm drivers/staging/prima/wlan.ko
./build_kernel_kenzo_losO_treble.sh || exit 1
#mv -f $KERNELDIR/BUILT_kenzo-losO_treble $TEMP_DIR/
rm .config
echo " "
echo "          BATCH MODE: Built AGNi LOS-O treble variant!!!"
echo "-----------------------------------------------------------------------"
echo " "

echo "-----------------------------------------------------------------------"
echo " "
echo "          BATCH MODE: Building AGNi PIE treble variant..."
# AGNI purePIE treble
rm drivers/staging/prima/wlan.ko
./build_kernel_kenzo_pie_treble.sh || exit 1
#mv -f $KERNELDIR/BUILT_kenzo-pie_treble $TEMP_DIR/
rm .config
echo " "
echo "          BATCH MODE: Built AGNi PIE treble variant!!!"
echo "-----------------------------------------------------------------------"
echo " "

echo "-----------------------------------------------------------------------"
echo " "
echo "          BATCH MODE: Building AGNi LOS-O variant..."
# AGNI pureLOS-O
rm drivers/staging/prima/wlan.ko
./build_kernel_kenzo_losO.sh || exit 1
#mv -f $KERNELDIR/BUILT_kenzo-losO $TEMP_DIR/
rm .config
echo " "
echo "          BATCH MODE: Built AGNi LOS-O variant!!!"
echo "-----------------------------------------------------------------------"
echo " "

echo " "
echo "-----------------------------------------------------------------------"
echo " "
echo "          BATCH MODE: Building AGNi MIUI-O variant..."
# AGNI pureMIUI-O
rm drivers/staging/prima/wlan.ko
./build_kernel_kenzo_miuiO.sh || exit 1
#mv -f $KERNELDIR/BUILT_kenzo-miuiO $TEMP_DIR/
rm .config
echo " "
echo "          BATCH MODE: Built AGNi MIUI-O variant!!!"
echo "-----------------------------------------------------------------------"
echo " "

echo "-----------------------------------------------------------------------"
echo " "
echo "          BATCH MODE: Building AGNi PIE variant..."
# AGNI purePIE
rm drivers/staging/prima/wlan.ko
./build_kernel_kenzo_pie.sh || exit 1
#mv -f $KERNELDIR/BUILT_kenzo-losO $TEMP_DIR/
rm .config
echo " "
echo "          BATCH MODE: Built AGNi PIE variant!!!"
echo "-----------------------------------------------------------------------"
echo " "
echo "-----------------------------------------------------------------------"
echo " "
echo "          BATCH MODE: Building AGNi LOS-N variant..."
# AGNI pureLOS-N
rm drivers/staging/prima/wlan.ko
./build_kernel_kenzo_losN.sh || exit 1
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
./build_kernel_kenzo_miuiN.sh || exit 1
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
./build_kernel_kenzo_miuiMM.sh || exit 1
#mv -f $KERNELDIR/BUILT_kenzo-miuiMM $TEMP_DIR/
rm .config
echo " "
echo "          BATCH MODE: Built AGNi MIUI-MM variant !!!"
echo "-----------------------------------------------------------------------"
echo " "

#mv -f $TEMP_DIR/* $KERNELDIR
#rm -rf $TEMP_DIR
echo "          BATCH MODE: Built ALL variants !!!"

