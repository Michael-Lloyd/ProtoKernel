#!/bin/bash

# flash_odroid_m2.sh - Flash Odroid M2 with U-boot and kernel
# Usage: flash_odroid_m2.sh <device> <kernel_binary>
# Example: flash_odroid_m2.sh /dev/sda build/arm64/kernel.bin

set -e

if [ $# -ne 2 ]; then
    echo "Usage: $0 <device> <kernel_binary>"
    echo "Example: $0 /dev/sda build/arm64/kernel.bin"
    exit 1
fi

DEVICE="$1"
KERNEL_BINARY="$2"
SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

# U-boot files
UBOOT_DIR="$REPO_ROOT/reference/u-boot-odroid-m2/build-odroid-m2-rk3588s"
IDBLOADER="$UBOOT_DIR/idbloader.img"
UBOOT_IMG="$UBOOT_DIR/u-boot.img"

# Check if we're running as root
if [ "$EUID" -ne 0 ]; then
    echo "This script must be run as root (use sudo)"
    exit 1
fi

# Verify device exists
if [ ! -b "$DEVICE" ]; then
    echo "Error: Device $DEVICE does not exist or is not a block device"
    exit 1
fi

# Verify kernel binary exists
if [ ! -f "$KERNEL_BINARY" ]; then
    echo "Error: Kernel binary $KERNEL_BINARY does not exist"
    exit 1
fi

# Verify U-boot files exist
if [ ! -f "$IDBLOADER" ]; then
    echo "Error: idbloader.img not found at $IDBLOADER"
    exit 1
fi

if [ ! -f "$UBOOT_IMG" ]; then
    echo "Error: u-boot.img not found at $UBOOT_IMG"
    exit 1
fi

echo "========================================"
echo "Flashing Odroid M2"
echo "========================================"
echo "Device: $DEVICE"
echo "Kernel: $KERNEL_BINARY"
echo "U-boot: $UBOOT_DIR"
echo "========================================"

# Confirm with user
read -p "WARNING: This will DESTROY ALL DATA on $DEVICE. Continue? [y/N] " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Cancelled."
    exit 1
fi

# Unmount any existing partitions
echo "Unmounting any existing partitions..."
umount ${DEVICE}* 2>/dev/null || true

# Wipe the beginning of the device
echo "Wiping device..."
dd if=/dev/zero of="$DEVICE" bs=1M count=16 conv=fsync

# Create partition table
echo "Creating partition table..."
parted "$DEVICE" --script -- mklabel gpt

# Create partitions:
# 1. Reserved space for U-boot (0-16MB)
# 2. Boot partition (16MB-144MB, FAT32) - for kernel, dtb, boot scripts
# 3. Root partition (144MB-end, ext4) - for kernel image and future rootfs

echo "Creating partitions..."
parted "$DEVICE" --script -- \
    mkpart primary 32768s 32767s \
    set 1 name "loader" \
    mkpart primary fat32 32768s 294912s \
    set 2 boot on \
    set 2 name "boot" \
    mkpart primary ext4 294912s 100% \
    set 3 name "rootfs"

# Wait for partition creation
sleep 2

# Determine partition names (handle both /dev/sdX and /dev/mmcblkX naming)
if [[ $DEVICE == *"mmcblk"* ]] || [[ $DEVICE == *"nvme"* ]]; then
    BOOT_PART="${DEVICE}p2"
    ROOT_PART="${DEVICE}p3"
else
    BOOT_PART="${DEVICE}2"
    ROOT_PART="${DEVICE}3"
fi

# Format partitions
echo "Formatting boot partition..."
mkfs.fat -F 32 -n "BOOT" "$BOOT_PART"

echo "Formatting root partition..."
mkfs.ext4 -L "rootfs" "$ROOT_PART"

# Flash U-boot components
echo "Flashing idbloader.img..."
dd if="$IDBLOADER" of="$DEVICE" seek=64 conv=notrunc,fsync

echo "Flashing u-boot.img..."
dd if="$UBOOT_IMG" of="$DEVICE" seek=16384 conv=notrunc,fsync

# Mount boot partition and copy kernel
echo "Mounting boot partition..."
BOOT_MOUNT=$(mktemp -d)
mount "$BOOT_PART" "$BOOT_MOUNT"

echo "Copying kernel to boot partition..."
cp "$KERNEL_BINARY" "$BOOT_MOUNT/kernel.bin"

# Create a basic boot script for U-boot
echo "Creating boot script..."
cat > "$BOOT_MOUNT/boot.cmd" << 'EOF'
# Odroid M2 boot script
echo "Loading kernel from boot partition..."

# Set kernel load address (adjust if needed for ARM64)
setenv kernel_addr_r 0x02080000

# Load kernel from boot partition
echo "Loading kernel.bin..."
load mmc 1:2 ${kernel_addr_r} kernel.bin

# Set kernel command line
setenv bootargs "console=ttyS2,1500000 earlycon=uart8250,mmio32,0xfeb50000"

# Boot the kernel
echo "Booting kernel..."
bootm ${kernel_addr_r}
EOF

# Convert boot script to U-boot format (if mkimage is available)
if command -v mkimage >/dev/null 2>&1; then
    echo "Converting boot script..."
    mkimage -C none -A arm64 -T script -d "$BOOT_MOUNT/boot.cmd" "$BOOT_MOUNT/boot.scr"
else
    echo "Warning: mkimage not found, boot.scr not created"
    echo "U-boot may not be able to find the boot script"
fi

# Create a simple README
cat > "$BOOT_MOUNT/README.txt" << EOF
Odroid M2 Boot Partition
========================

This partition contains:
- kernel.bin: The kernel binary
- boot.cmd: U-boot boot script (source)
- boot.scr: U-boot boot script (compiled)

The U-boot bootloader will automatically load and execute boot.scr,
which will load kernel.bin and boot the system.

To modify the boot process, edit boot.cmd and regenerate boot.scr:
mkimage -C none -A arm64 -T script -d boot.cmd boot.scr
EOF

# Unmount boot partition
umount "$BOOT_MOUNT"
rmdir "$BOOT_MOUNT"

# Mount root partition and copy kernel (for reference)
echo "Mounting root partition..."
ROOT_MOUNT=$(mktemp -d)
mount "$ROOT_PART" "$ROOT_MOUNT"

echo "Copying kernel to root partition..."
cp "$KERNEL_BINARY" "$ROOT_MOUNT/"

# Create a simple info file
cat > "$ROOT_MOUNT/kernel_info.txt" << EOF
Kernel Information
==================

Kernel binary: $(basename "$KERNEL_BINARY")
Flashed on: $(date)
Device: $DEVICE

This partition can be used for a future root filesystem.
The kernel is also stored here for reference.
EOF

# Unmount root partition
umount "$ROOT_MOUNT"
rmdir "$ROOT_MOUNT"

# Sync to ensure all data is written
echo "Syncing..."
sync

echo ""
echo "========================================"
echo "Flashing completed successfully!"
echo "========================================"
echo "Device: $DEVICE"
echo "Partitions created:"
echo "  ${BOOT_PART}: Boot partition (FAT32) - contains kernel.bin and boot script"
echo "  ${ROOT_PART}: Root partition (ext4) - available for future rootfs"
echo ""
echo "U-boot components flashed:"
echo "  idbloader.img at sector 64"
echo "  u-boot.img at sector 16384"
echo ""
echo "The SD card is ready to boot on Odroid M2"
echo "Insert the SD card and power on the device"
echo "========================================"