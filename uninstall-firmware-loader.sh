#!/bin/sh

set -eu

HELPER_DIR="/usr/local/libexec/foo2zjs"
HELPER_PATH="$HELPER_DIR/foo2zjs-firmware-loader"
PLIST_PATH="/Library/LaunchDaemons/org.foo2zjs.firmware-loader.plist"
LABEL="org.foo2zjs.firmware-loader"

if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: Run this uninstaller with sudo." >&2
    exit 1
fi

if launchctl print "system/$LABEL" >/dev/null 2>&1; then
    launchctl bootout "system/$LABEL"
fi

rm -f "$PLIST_PATH"
rm -f "$HELPER_PATH"
rmdir "$HELPER_DIR" 2>/dev/null || true

echo "Removed the foo2zjs firmware loader and LaunchDaemon."
echo "Firmware files and CUPS queues were left unchanged."
