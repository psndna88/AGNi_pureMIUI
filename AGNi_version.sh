#!/bin/sh

## AGNi version info
KERNELDIR=`readlink -f .`

export AGNI_VERSION_PREFIX="stable"
export AGNI_VERSION="v11.9.1"
sed -i 's/v11.9_stable-EAS/v11.9.1_stable-EAS/' $KERNELDIR/arch/arm64/configs/agni_*
sed -i 's/ini_set("rom_version",	"v11.9_stable");/ini_set("rom_version",	"v11.9.1_stable");/' $KERNELDIR/anykernel3/META-INF/com/google/android/aroma-config

echo "	AGNi Version info loaded."

# CCACHE SHIFTING
if [ -d ~/.ccache ] && [ -d ~/.ccache_MIATOLL ]; then
	echo "	   C-CACHE is already set to SDM660."
elif [ -d ~/.ccache_SDM660 ] && [ -d ~/.ccache ]; then
	mv ~/.ccache ~/.ccache_MIATOLL
	mv ~/.ccache_SDM660 ~/.ccache
	echo "	   C-CACHE was shifted to SDM660 !"
else
	mkdir ~/.ccache_MIATOLL
	echo "	   New C-CACHE created for SDM660 !"
fi
