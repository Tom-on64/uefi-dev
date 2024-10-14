#!/bin/bash

# Constants
IMG_NAME=${1:-'disk.img'}
IMG_SIZE=128     # In megabytes
EFI_BOOT='BOOTX64.EFI'
FILESYSTEM='osfs.bin'

# Create blank disk image
dd if=/dev/zero of="$IMG_NAME" bs=1M count=${IMG_SIZE}

# Partition the disk
sgdisk -Z $IMG_NAME
sgdisk --new=1:2048:64M --typecode=1:EF00 --change-name=1:'EFI SYSTEM'  $IMG_NAME
sgdisk --new=2:0:0      --typecode=2:8300 --change-name=2:'OS DATA'     $IMG_NAME

# Attach disk as a device in /dev
DEV=$( hdiutil attach -nomount $IMG_NAME | grep -Eo '/dev/disk[0-9]+' | head -1 )

# Format first partition as FAT32 FS (This will also mount it)
diskutil eraseVolume FAT32 ESP "${DEV}s1"

mkdir -p /Volumes/ESP/EFI/BOOT
cp $EFI_BOOT /Volumes/ESP/EFI/BOOT

# Unmount the partition
umount /Volumes/ESP

# Copy OS filesystem to OS DATA partition
dd if="$FILESYSTEM" of="${DEV}s2" bs=1M

# Detach disk image and cleanup
hdiutil detach "$DEV"

echo "Disk image $IMG_NAME was built succesfully!"

