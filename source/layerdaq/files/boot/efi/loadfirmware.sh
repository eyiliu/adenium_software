#! /usr/bin/env sh
DTBO_BASE_NAME="altel_plane_2p10"
if [ $# -gt 0 ]
then
    DTBO_BASE_NAME=$1
fi

if [ ! -f /usr/lib/firmware/${DTBO_BASE_NAME}.dtbo ]
then
    echo "unable to find /usr/lib/firmware/${DTBO_BASE_NAME}.dtbo"
    exit -1
fi

if [ -d /configfs/device-tree/overlays/full ]
then
    rmdir /configfs/device-tree/overlays/full
fi

echo 0 > /sys/class/fpga_manager/fpga0/flags

FINDMNT_RE=$(findmnt -T /configfs -t configfs)
if [ $? != 0 ]
then
    echo "mounting configfs"
    mount -t configfs configfs /configfs
fi

mkdir -p /configfs/device-tree/overlays/full

echo "loading ${DTBO_BASE_NAME}"
echo -n "${DTBO_BASE_NAME}.dtbo" > /configfs/device-tree/overlays/full/path
