#!/bin/bash
date=$(date +%d-%m-%y)
# Working Directory
dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
# Destination Directory
dest=~/Output/HTC10_Kernel
# Modules
moudles=~/Output/HTC10_Modules
# zImage
zImage=~/Output/HTC10_zImage
# Configuration Name
config=msm_defconfig
# git clone https://android.googlesource.com/platform/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9

#Set Path, cross compile and user
export PATH=~/toolchains/aarch64-linux-android-4.9/bin/:$PATH
export ARCH=arm64
export SUBARCH=arm
export CROSS_COMPILE=aarch64-linux-android-
export KBUILD_BUILD_USER=root

rm -rf $zImage > /dev/null
rm -rf $modules > /dev/null

#Get Version Number
echo "Please enter version number: "
read version

#Ask user if they would like to clean
read -p "Would you like to clean (y/n)? " -n 1 -r
echo    
if [[ ! $REPLY =~ ^[Nn]$ ]]
then
    rm -r $dest  > /dev/null
fi

#Set Local Version String to kernel configuration
rm .version > /dev/null
VER="_Clumsy-V$version"
DATE_START=$(date +"%s")
mkdir -p $dest

# Make the configuration
make O=$dest $config

str="CONFIG_LOCALVERSION=\"$VER\""
sed -i "45s/.*/$str/" $dest/.config
read -p "Would you like to see menu config (y/n)? " -n 1 -r
echo 
   
if [[ $REPLY =~ ^[Yy]$ ]]
then
    make menuconfig
fi 

# Make
make O=$dest -j16

DATE_END=$(date +"%s")
DIFF=$(($DATE_END - $DATE_START))
echo
if (( $(($DIFF / 60)) == 0 )); then
echo " Build completed in $(($DIFF % 60)) seconds."
elif (( $(($DIFF / 60)) == 1 )); then
echo " Build completed in $(($DIFF / 60)) minute and $(($DIFF % 60)) seconds."
else
echo " Build completed in $(($DIFF / 60)) minutes and $(($DIFF % 60)) seconds."
fi
echo " Finish time: $(date +"%r")"
echo

# Get the modules
echo "Moving modules to $modules"
mkdir -p $moudles
find $dest -name '*ko' -exec cp '{}' $moudles \;

# Copy Image
echo "Moving Image.gz-dtb to $zImage"
mkdir -p $zImage
cp $dest/arch/arm64/boot/Image.gz-dtb $zImage/.

echo "-> Done"

