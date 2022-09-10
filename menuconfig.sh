#!/bin/bash
export KERNELDIR=`readlink -f .`
CONFIG1="agni_haydn_defconfig"
. $KERNELDIR/AGNi_version.sh
if [ -f ~/WORKING_DIRECTORY/AGNi_stamp.sh ];
	then
	. ~/WORKING_DIRECTORY/AGNi_stamp.sh
fi
if [ -f ~/WORKING_DIRECTORY/snapdragon_llvm.sh ]; then
	. ~/WORKING_DIRECTORY/snapdragon_llvm.sh
else
	export CROSS_COMPILE=/PATH_TO/snapdragon_llvm_aarch64_v14.1.4/bin/aarch64-linux-android-
	export CROSS_COMPILE_ARM32=/PATH_TO/snapdragon_llvm_arm_v14.1.4/bin/arm-linux-androideabi-
	export CLANG_TRIPLE=aarch64-linux-gnu
	#32bit VDSO
	export CROSS_COMPILE_COMPAT=/PATH_TO/snapdragon_llvm_arm_v14.1.4/bin/arm-linux-androideabi-
fi
export LD="$CROSS_COMPILEld.lld"
export CC="$CROSS_COMPILEclang"
export NM="$CROSS_COMPILEllvm-nm"
export OBJCOPY="$CROSS_COMPILEllvm-objcopy"

if [ ! -f $COMPILEDIR_HAYDN/.config ]; then
	cp $KERNELDIR/arch/arm64/configs/$CONFIG1 $COMPILEDIR_HAYDN/.config
fi

if [ ! -d $COMPILEDIR_HAYDN ]; then
	COUT=$KERNELDIR/OUTPUT
	mkdir $COUT
else
	COUT=$COMPILEDIR_HAYDN
fi

if [ -f $COUT/.config ];
	then
	make menuconfig O=$COUT ARCH=arm64
else
	exit 1
fi

