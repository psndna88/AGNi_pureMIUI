#!/bin/bash
COUT="~/WORKING_DIRECTORY/COMPILED_OUT"
KERNELDIR=`readlink -f .`

if [ -f ~/WORKING_DIRECTORY/AGNi_stamp.sh ]; then
	. ~/WORKING_DIRECTORY/AGNi_stamp.sh
fi

chmod +x $KERNELDIR/AGNi_version.sh
chmod +x $KERNELDIR/cleanconfig.sh
chmod +x $KERNELDIR/cleanbuild.sh
chmod +x $KERNELDIR/menuconfig.sh
chmod +x $KERNELDIR/scripts/agni/*

HORIZONTALLINE="-----------------------------------------------------------------------"
clear
echo "$HORIZONTALLINE"
echo " "
echo "        AGNi kernel by psndna88 !!!"
echo $HORIZONTALLINE
echo " 1: Build All Devices"
echo " 2: Build whyred   - Redmi Note 5 Pro"
echo " 3: Build tulip    - Redmi Note 6 Pro"
echo " 4: Build lavender - Redmi Note 7"
echo " 5: Build wayne    - MI 6X"
echo " 6: Build jasmine  - MI A2"
echo " "
echo " 7: Build whyred ROG MIUI-Q - Redmi Note 5 Pro"
echo " 8: Build whyred ROG MIUI-R - Redmi Note 5 Pro"
echo " 9: Build whyred Hellas MIUI-R - Redmi Note 5 Pro"
echo " "
echo " 0:  X  Exit Compilation  X"
echo " "
echo "$HORIZONTALLINE"
read -p "    Select what to build : " choice

if [ $choice -eq 1 ]; then
	#### ALL Devices
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi whyred variant..."
	./scripts/agni/build_kernel_whyred.sh
	echo " "
	echo "          BATCH MODE: Built AGNi whyred variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo " "
	echo "          BATCH MODE: Building AGNi whyred ROG Q variant..."
	./scripts/agni/build_kernel_whyred_ROG-Q.sh
	echo " "
	echo "          BATCH MODE: Built AGNi whyred ROG Q variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo " "
	echo "          BATCH MODE: Building AGNi whyred ROG R variant..."
	./scripts/agni/build_kernel_whyred_ROG-R.sh
	echo " "
	echo "          BATCH MODE: Built AGNi whyred ROG R variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo " "
	echo "          BATCH MODE: Building AGNi whyred Hellas R variant..."
	./scripts/agni/build_kernel_whyred_hellas-R.sh
	echo " "
	echo "          BATCH MODE: Built AGNi whyred Hellas R variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi tulip variant..."
	./scripts/agni/build_kernel_tulip.sh
	echo " "
	echo "          BATCH MODE: Built AGNi tulip variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi lavender variant..."
	./scripts/agni/build_kernel_lavender.sh
	echo " "
	echo "          BATCH MODE: Built AGNi lavender variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi wayne variant..."
	./scripts/agni/build_kernel_wayne.sh
	echo " "
	echo "          BATCH MODE: Built AGNi wayne variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo "          BATCH MODE: Building AGNi jasmine variant..."
	./scripts/agni/build_kernel_jasmine.sh
	echo " "
	echo "          BATCH MODE: Built AGNi jasmine variant!!!"
	echo $HORIZONTALLINE
	echo " "
elif [ $choice -eq 2 ]; then
	#### WHYRED
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi whyred variant..."
	./scripts/agni/build_kernel_whyred.sh
	echo " "
	echo "          BATCH MODE: Built AGNi whyred variant!!!"
	echo $HORIZONTALLINE
	echo " "
elif [ $choice -eq 3 ];	then
	#### TULIP
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi tulip variant..."
	./scripts/agni/build_kernel_tulip.sh
	echo " "
	echo "          BATCH MODE: Built AGNi tulip variant!!!"
	echo $HORIZONTALLINE
	echo " "
elif [ $choice -eq 4 ];	then
	#### LAVENDER
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi lavender variant..."
	./scripts/agni/build_kernel_lavender.sh
	echo " "
	echo "          BATCH MODE: Built AGNi lavender variant!!!"
	echo $HORIZONTALLINE
	echo " "
elif [ $choice -eq 5 ];	then
	#### WAYNE
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi wayne variant..."
	./scripts/agni/build_kernel_wayne.sh
	echo " "
	echo "          BATCH MODE: Built AGNi wayne variant!!!"
	echo $HORIZONTALLINE
	echo " "
elif [ $choice -eq 6 ]; then
	#### JASMINE
	echo $HORIZONTALLINE
	echo " "
	echo "          BATCH MODE: Building AGNi jasmine variant..."
	./scripts/agni/build_kernel_jasmine.sh
	echo " "
	echo "          BATCH MODE: Built AGNi jasmine variant!!!"
	echo $HORIZONTALLINE
	echo " "
elif [ $choice -eq 7 ]; then
	#### whyred ROG Q
	echo $HORIZONTALLINE
	echo " "
	echo "          BATCH MODE: Building AGNi whyred ROG Q variant..."
	./scripts/agni/build_kernel_whyred_ROG-Q.sh
	echo " "
	echo "          BATCH MODE: Built AGNi whyred ROG Q variant!!!"
	echo $HORIZONTALLINE
	echo " "
elif [ $choice -eq 8 ]; then
	#### whyred ROG R
	echo $HORIZONTALLINE
	echo " "
	echo "          BATCH MODE: Building AGNi whyred ROG R variant..."
	./scripts/agni/build_kernel_whyred_ROG-R.sh
	echo " "
	echo "          BATCH MODE: Built AGNi whyred ROG R variant!!!"
	echo $HORIZONTALLINE
	echo " "
elif [ $choice -eq 9 ]; then
	#### whyred Hellas R
	echo $HORIZONTALLINE
	echo " "
	echo "          BATCH MODE: Building AGNi whyred Hellas R variant..."
	./scripts/agni/build_kernel_whyred_hellas-R.sh
	echo " "
	echo "          BATCH MODE: Built AGNi whyred Hellas R variant!!!"
	echo $HORIZONTALLINE
	echo " "
elif [ $choice -eq 0 ]; then
	exit
else
	echo " "
	echo -e "====> Enter corrent input <===="
fi
