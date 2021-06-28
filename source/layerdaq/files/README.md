# kernel boot files:
cp image.ub /boot/efi/
cp system.dtb /boot/efi/
cp uEnv.txt /boot/efi/

# firmware load at bootup
cp adenium-firmware.service /etc/systemd/system/
cp loadfirmware.sh /boot/efi/
cp altel_plane_2p10.dtbo /usr/lib/firmware/altel_plane_2p10.dtbo
cp altel_plane_2p10.dtbo /usr/lib/firmware/altel_plane_2p10.bit.bin
systemctl enable adenium-firmware


# daq tcp server starup at bootup.
# NOTE: change the adenium-tcpserver.service for different layer id.
# TODO: improve testserver, use a config file alongside testserver.
cp adenium-tcpserver.service /etc/systemd/system/
cp testserver /boot/efi/
systemctl enable adenium-tcpserver
