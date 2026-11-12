#!/bin/sh
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
#
# Upload a signed image to the ECB02C through the serial recovery of MCUboot without touching
# the power.
#
# The running application gets "kernel reboot cold" on its shell (the loader ignores it), MCUboot
# waits for a command after the start and takes the image, then the chip is reset. Needs a cmcumgr
# that repeats a command after a timeout: the window of MCUboot is short and a lost frame stops the
# upload of the stock client.
#
# usage: phyflash.sh <zephyr.signed.bin>
#   PORT, BAUD, CMCUMGR select the serial port (/dev/ttyUSB0, 115200) and the client (cmcumgr)

PORT="${PORT:-/dev/ttyUSB0}"
BAUD="${BAUD:-115200}"
CMCUMGR="${CMCUMGR:-cmcumgr}"

if [ ! -f "$1" ]; then
	echo "usage: $0 <zephyr.signed.bin>"
	exit 1
fi

stty -F "$PORT" "$BAUD" raw -echo 2> /dev/null
printf '\r\nkernel reboot cold\r\n' > "$PORT"
sleep 0.2

"$CMCUMGR" --conntype serial -s dev="$PORT",baud="$BAUD" -t 1 --retries 30 \
	image upload "$1" || exit $?
sleep 0.5
"$CMCUMGR" --conntype serial -s dev="$PORT",baud="$BAUD" -t 2 --retries 4 reset
