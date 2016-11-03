#!/bin/sh
export KERNELDIR=`readlink -f .`
cd $KERNELDIR;

TEMP_DIR=$KERNELDIR/../TEMP_KENZO_BUILDS_RUN_STORE
mkdir -p $TEMP_DIR
rm -rf BUILT_kenzo-miuiMM BUILT_kenzo-cmMM BUILT_kenzo-cmN BUILT_kenzo-cmNwm .config

echo " "
echo "-----------------------------------------------------------------------"
echo " "
echo "          BATCH MODE: Building all AGNi MM variants..."

# AGNI pureCM-MM
rm drivers/staging/prima/wlan.ko
./build_kernel_kenzo_cmMM.sh
mv -f $KERNELDIR/BUILT_kenzo-cmMM $TEMP_DIR
rm .config

echo "-----------------------------------------------------------------------"
echo " "

# AGNI pureMIUI-MM
rm drivers/staging/prima/wlan.ko
./build_kernel_kenzo_miuiMM.sh
mv -f $KERNELDIR/BUILT_kenzo-miuiMM $TEMP_DIR
rm .config

mv -f $TEMP_DIR/* $KERNELDIR
rm -rf $TEMP_DIR
echo " "
echo "          BATCH MODE: Built all AGNi MM variants !!!"
echo "-----------------------------------------------------------------------"
echo " "
sleep 3
echo " "
echo "-----------------------------------------------------------------------"
echo " "
echo "          BATCH MODE: Building all AGNi N variants..."

# AGNI pureCM-Nwm
rm drivers/staging/prima/wlan.ko
./build_kernel_kenzo_cmNwm.sh
mv -f $KERNELDIR/BUILT_kenzo-cmNwm $TEMP_DIR
rm .config

echo "-----------------------------------------------------------------------"
echo " "

# AGNI pureCM-N
rm drivers/staging/prima/wlan.ko
./build_kernel_kenzo_cmN.sh
mv -f $KERNELDIR/BUILT_kenzo-cmN $TEMP_DIR
rm .config

mv -f $TEMP_DIR/* $KERNELDIR
rm -rf $TEMP_DIR
echo " "
echo "          BATCH MODE: Built all AGNi N variants !!!"
echo "-----------------------------------------------------------------------"
echo " "

