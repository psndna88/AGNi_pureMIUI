#!/bin/bash

# Custom build script for Escrima Kernel

# Constants
green='\033[01;32m'
red='\033[01;31m'
blink_red='\033[05;31m'
cyan='\033[0;36m'
yellow='\033[0;33m'
blue='\033[0;34m'
default='\033[0m'

# Define variables
KERNEL_DIR=$PWD
Anykernel_DIR=$KERNEL_DIR/AnyKernel3/
DATE=$(date +"%d%m%Y")
TIME=$(date +"-%H.%M.%S")
KERNEL_NAME="Escrima-X28-Bloody-Mod"
DEVICE="-kenzo-"
FINAL_ZIP="$KERNEL_NAME""$DEVICE""$DATE""$TIME"

BUILD_START=$(date +"%s")

# Cleanup before
rm -rf $Anykernel_DIR/*zip
rm -rf $Anykernel_DIR/Image.gz-dtb
rm -rf arch/arm64/boot/Image
rm -rf arch/arm64/boot/dts/qcom/kenzo-msm8956-mtp.dtb
rm -rf arch/arm64/boot/Image.gz
rm -rf arch/arm64/boot/Image.gz-dtb

# Export few variables
export KBUILD_BUILD_USER="AmolAmrit"
export KBUILD_BUILD_HOST="Nightwing"
export CROSS_COMPILE=/home/amol/pie/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-
export ARCH="arm64"
export USE_CCACHE=1

echo -e "$green***********************************************"
echo  "           Compiling Escrima Kernel                    "
echo -e "***********************************************"

# Finally build it
make clean && make mrproper
make lineageos_kenzo_defconfig
make -j6

echo -e "$yellow***********************************************"
echo  "                 Zipping up                    "
echo -e "***********************************************"

# Create the flashable zip
cp arch/arm64/boot/Image.gz-dtb $Anykernel_DIR
cd $Anykernel_DIR
zip -r9 $FINAL_ZIP.zip * -x .git README.md *placeholder

echo -e "$cyan***********************************************"
echo  "            Cleaning up the mess created               "
echo -e "***********************************************$default"

# Cleanup again
cd ../
rm -rf $Anykernel_DIR/Image.gz-dtb
rm -rf arch/arm64/boot/Image
rm -rf arch/arm64/boot/dts/qcom/kenzo-msm8956-mtp.dtb
rm -rf arch/arm64/boot/Image.gz
rm -rf arch/arm64/boot/Image.gz-dtb
make clean && make mrproper

# Build complete
BUILD_END=$(date +"%s")
DIFF=$(($BUILD_END - $BUILD_START))
echo -e "$green Build completed in $(($DIFF / 60)) minute(s) and $(($DIFF % 60)) seconds.$default"
