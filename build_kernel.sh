#!/bin/sh
COUT="/mnt/ANDROID/COMPILED_OUT"
KERNELDIR=`readlink -f .`

if [ -f $COUT/.config ];
then
	rm $COUT/.config
	rm $COUT/.config.old
fi

chmod +x $KERNELDIR/AGNi_version.sh
chmod +x $KERNELDIR/cleanconfig.sh
chmod +x $KERNELDIR/cleanbuild.sh
chmod +x $KERNELDIR/menuconfig.sh
chmod +x $KERNELDIR/scripts/agni/*

HORIZONTALLINE="-----------------------------------------------------------------------"
clear
echo "\n$HORIZONTALLINE"
echo "        AGNi kernel by psndna88 !!!"
echo $HORIZONTALLINE
echo " 1: Build All Devices"
echo " 2: Quick Build only whyred (Q-OldCAM)"
echo " 3: Build all whyred   - Redmi Note 5 Pro"
echo " 4: Build all tulip    - Redmi Note 6 Pro"
echo " 5: Build all lavender - Redmi Note 7"
echo " 6: Build all wayne    - MI 6X"
echo " 7: Build jasmine      - MI A2"
echo " "
echo " 0:  X  Exit Compilation  X"
echo "\n$HORIZONTALLINE"
read -p "    Select what to build : " choice

if [ $choice -eq 1 ]; then
	#### ALL Devices
	#### --ALL WHYRED
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi whyred Q variant..."
	./scripts/agni/build_kernel_whyred_Q.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi whyred Q variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi whyred Q OldCam variant..."
	./scripts/agni/build_kernel_whyred_Q-oldcam.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi whyred Q OldCam variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi whyred Pie variant..."
	./scripts/agni/build_kernel_whyred_pie.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi whyred Pie variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi whyred Pie OldCam variant..."
	./scripts/agni/build_kernel_whyred_pie-oldcam.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi whyred Pie Old Cam variant!!!"
	echo $HORIZONTALLINE
	echo " "
	#### --ALL TULIP
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi tulip Q variant..."
	./scripts/agni/build_kernel_tulip_Q.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi tulip Q variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi tulip Q OldCam variant..."
	./scripts/agni/build_kernel_tulip_Q-oldcam.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi tulip Q OldCam variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi tulip Pie variant..."
	./scripts/agni/build_kernel_tulip_pie.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi tulip Pie variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi tulip Pie OldCam variant..."
	./scripts/agni/build_kernel_tulip_pie-oldcam.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi tulip Pie OldCam variant!!!"
	echo $HORIZONTALLINE
	echo " "
	#### ALL LAVENDER
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi lavender Q variant..."
	./scripts/agni/build_kernel_lavender_Q.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi lavender Q variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi lavender Q OldCam variant..."
	./scripts/agni/build_kernel_lavender_Q-oldcam.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi lavender Q OldCam variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi lavender MIUI Q variant..."
	./scripts/agni/build_kernel_lavender_MIUI-Q.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi lavender MIUI Q variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi lavender Pie variant..."
	./scripts/agni/build_kernel_lavender_pie.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi lavender Pie variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi lavender Pie OldCam variant..."
	./scripts/agni/build_kernel_lavender_pie-oldcam.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi lavender Pie OldCam variant!!!"
	echo $HORIZONTALLINE
	echo " "
	#### --ALL WAYNE
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi wayne Q variant..."
	./scripts/agni/build_kernel_wayne_Q.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi wayne Q variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi wayne Q OldCam variant..."
	./scripts/agni/build_kernel_wayne_Q-oldcam.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi wayne Q OldCam variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi wayne MIUI Q variant..."
	./scripts/agni/build_kernel_wayne_MIUI-Q.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi wayne MIUI Q variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi wayne Pie variant..."
	./scripts/agni/build_kernel_wayne_pie.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi wayne Pie variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi wayne Pie OldCam variant..."
	./scripts/agni/build_kernel_wayne_pie-oldcam.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi wayne Pie OldCam variant!!!"
	echo $HORIZONTALLINE
	echo " "
	#### --ALL JASMINE
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi jasmine Q variant..."
	./scripts/agni/build_kernel_jasmine_Q.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi jasmine Q variant!!!"
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Built ALL variants !!!"
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi jasmine MIUI Q variant..."
	./scripts/agni/build_kernel_jasmine_MIUI-Q.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi jasmine MIUI Q variant!!!"
	echo $HORIZONTALLINE
elif [ $choice -eq 2 ]; then
	#### --QUICK WHYRED Q NEWCAM
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi whyred Q OldCam variant..."
	./scripts/agni/build_kernel_whyred_Q-oldcam.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi whyred Q OldCam variant!!!"
	echo $HORIZONTALLINE		
elif [ $choice -eq 3 ]; then
	#### ALL WHYRED
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi whyred Q variant..."
	./scripts/agni/build_kernel_whyred_Q.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi whyred Q variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi whyred Q OldCam variant..."
	./scripts/agni/build_kernel_whyred_Q-oldcam.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi whyred Q OldCam variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi whyred Pie variant..."
	./scripts/agni/build_kernel_whyred_pie.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi whyred Pie variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi whyred Pie OldCam variant..."
	./scripts/agni/build_kernel_whyred_pie-oldcam.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi whyred Pie Old Cam variant!!!"
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Built ALL WHYRED variants !!!"
elif [ $choice -eq 4 ];	then
	#### ALL TULIP
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi tulip Q variant..."
	./scripts/agni/build_kernel_tulip_Q.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi tulip Q variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi tulip Q OldCam variant..."
	./scripts/agni/build_kernel_tulip_Q-oldcam.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi tulip Q OldCam variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi tulip Pie variant..."
	./scripts/agni/build_kernel_tulip_pie.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi tulip Pie variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi tulip Pie OldCam variant..."
	./scripts/agni/build_kernel_tulip_pie-oldcam.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi tulip Pie OldCam variant!!!"
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Built ALL TULIP variants !!!"
elif [ $choice -eq 5 ];	then
	#### ALL LAVENDER
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi lavender Q variant..."
	./scripts/agni/build_kernel_lavender_Q.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi lavender Q variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi lavender Q OldCam variant..."
	./scripts/agni/build_kernel_lavender_Q-oldcam.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi lavender Q OldCam variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi lavender MIUI Q variant..."
	./scripts/agni/build_kernel_lavender_MIUI-Q.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi lavender MIUI Q variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi lavender Pie variant..."
	./scripts/agni/build_kernel_lavender_pie.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi lavender Pie variant!!!"
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Built ALL LAVENDER variants !!!"
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi lavender Pie OldCam variant..."
	./scripts/agni/build_kernel_lavender_pie-oldcam.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi lavender Pie OldCam variant!!!"
	echo $HORIZONTALLINE
	echo " "
elif [ $choice -eq 6 ];	then
	#### ALL WAYNE
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi wayne Q variant..."
	./scripts/agni/build_kernel_wayne_Q.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi wayne Q variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi wayne Q OldCam variant..."
	./scripts/agni/build_kernel_wayne_Q-oldcam.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi wayne Q OldCam variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi wayne MIUI Q variant..."
	./scripts/agni/build_kernel_wayne_MIUI-Q.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi wayne MIUI Q variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi wayne Pie variant..."
	./scripts/agni/build_kernel_wayne_pie.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi wayne Pie variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi wayne Pie OldCam variant..."
	./scripts/agni/build_kernel_wayne_pie-oldcam.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi wayne Pie OldCam variant!!!"
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Built ALL WAYNE variants !!!"
elif [ $choice -eq 7 ]; then
	#### ALL JASMINE
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi jasmine Q variant..."
	./scripts/agni/build_kernel_jasmine_Q.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi jasmine Q variant!!!"
	echo $HORIZONTALLINE
	echo " "
	echo $HORIZONTALLINE
	echo "          BATCH MODE: Building AGNi jasmine MIUI Q variant..."
	./scripts/agni/build_kernel_jasmine_MIUI-Q.sh || exit 1
	rm $COUT/.config $COUT/.config.old
	echo "          BATCH MODE: Built AGNi jasmine MIUI Q variant!!!"
	echo $HORIZONTALLINE
elif [ $choice -eq 0 ]; then
	exit
else
	echo -e "\n====> Enter corrent input <===="
fi

