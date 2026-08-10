#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
HELPER_NAME="foo2zjs-firmware-loader"
HELPER_DIR="/usr/local/libexec/foo2zjs"
HELPER_PATH="$HELPER_DIR/$HELPER_NAME"
FIRMWARE_DIR="/usr/local/share/foo2zjs/firmware"
FIRMWARE_PATH="$FIRMWARE_DIR/sihp1018.dl"
PLIST_NAME="org.foo2zjs.firmware-loader.plist"
PLIST_PATH="/Library/LaunchDaemons/$PLIST_NAME"
LABEL="org.foo2zjs.firmware-loader"
TEMP_DIR=""

cleanup() {
    if [ -n "$TEMP_DIR" ] && [ -d "$TEMP_DIR" ]; then
        rm -rf "$TEMP_DIR"
    fi
}
trap cleanup EXIT HUP INT TERM

if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: Run this installer with sudo." >&2
    exit 1
fi

if [ "$(uname -m)" != "arm64" ]; then
    echo "ERROR: The firmware loader currently supports Apple Silicon only." >&2
    exit 1
fi

make -C "$SCRIPT_DIR" "$HELPER_NAME"

install -d -o root -g wheel -m 755 "$HELPER_DIR"
install -o root -g wheel -m 755 "$SCRIPT_DIR/$HELPER_NAME" "$HELPER_PATH"
codesign --force --sign - "$HELPER_PATH"

install -d -o root -g wheel -m 755 "$FIRMWARE_DIR"
if [ ! -f "$FIRMWARE_PATH" ]; then
    echo "Generating HP LaserJet 1018 firmware..."
    make -C "$SCRIPT_DIR" arm2hpdl
    TEMP_DIR=$(mktemp -d /tmp/foo2zjs-fw-install.XXXXXX)
    "$SCRIPT_DIR/arm2hpdl" "$SCRIPT_DIR/foo2zjs/sihp1018.img" \
        > "$TEMP_DIR/sihp1018.dl"
    install -o root -g wheel -m 644 \
        "$TEMP_DIR/sihp1018.dl" "$FIRMWARE_PATH"
fi

if launchctl print "system/$LABEL" >/dev/null 2>&1; then
    launchctl bootout "system/$LABEL"
fi

install -o root -g wheel -m 644 \
    "$SCRIPT_DIR/macos/$PLIST_NAME" "$PLIST_PATH"
plutil -lint "$PLIST_PATH"
launchctl bootstrap system "$PLIST_PATH"
launchctl kickstart -k "system/$LABEL"

echo "Installed $HELPER_PATH"
echo "Installed $PLIST_PATH"
echo "Firmware: $FIRMWARE_PATH"
echo "Log: /var/log/foo2zjs-firmware-loader.log"
