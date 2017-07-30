#
# Custom build script 
#
Start=$(date +"%s")
KERNEL_DIR=$PWD
DTBTOOL=$KERNEL_DIR/dtbTool
cd $KERNEL_DIR
export ARCH=arm64
export CROSS_COMPILE="/home/xeondead/build/toolchains/aarch64-linux-android-6.x/bin/aarch64-linux-android-"
export LD_LIBRARY_PATH=/home/xeondead/build/toolchains/aarch64-linux-android-6.x/lib
STRIP="/home/xeondead/build/toolchains/aarch64-linux-android-6.x/bin/aarch64-linux-android-strip"
#make clean
make agni_kenzo-cmN_defconfig
make -j4
time=$(date +"%d-%m-%y-%T")
