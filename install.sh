#!/bin/sh
#
# printer-all — Install CUPS raster filters for 89 printers
#
# Compiles and installs all 9 format-family filters, PPDs, firmware,
# and ICM color profiles. No Ghostscript required — uses macOS's
# built-in cgpdftoraster.
#
# Usage: sudo ./install.sh [--firmware-autoload]
#

set -e

usage() {
    echo "Usage: sudo $0 [--firmware-autoload]"
    echo ""
    echo "Options:"
    echo "  --firmware-autoload  Also install the tested HP LaserJet 1018"
    echo "                       firmware LaunchDaemon."
    echo "  --help               Show this help."
}

FIRMWARE_AUTOLOAD=0
for arg in "$@"; do
    case "$arg" in
        --firmware-autoload)
            FIRMWARE_AUTOLOAD=1
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "ERROR: Unknown option: $arg" >&2
            usage >&2
            exit 2
            ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FILTER_DIR="/usr/libexec/cups/filter"
PPD_DIR="/Library/Printers/PPDs/Contents/Resources"
FIRMWARE_DIR="/usr/local/share/foo2zjs/firmware"
ICM_DIR="/usr/local/share/foo2zjs/icm"
FOO2ZJS_DIR="$SCRIPT_DIR/foo2zjs"

FILTERS="rastertoxqx rastertozjs rastertohiperc rastertoqpdl rastertolava rastertohbpl2 rastertohp rastertooak rastertoslx"

echo "=== printer-all — CUPS Filter Installer ==="
echo ""

# Check we're running as root
if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: This script must be run as root (use sudo)."
    echo "  sudo $0"
    exit 1
fi

# Check architecture
ARCH=$(uname -m)
if [ "$ARCH" != "arm64" ]; then
    echo "WARNING: These filters are built for Apple Silicon (arm64)."
    echo "  Detected architecture: $ARCH"
    echo ""
fi
if [ "$FIRMWARE_AUTOLOAD" -eq 1 ] && [ "$ARCH" != "arm64" ]; then
    echo "ERROR: The firmware autoloader currently supports Apple Silicon only."
    exit 1
fi

# Step 1: Compile filters
echo "[1/5] Compiling filters..."
for f in $FILTERS; do
    if [ ! -f "$SCRIPT_DIR/$f" ] || [ "$(file -b "$SCRIPT_DIR/$f" | grep -c arm64)" -eq 0 ]; then
        clang -o "$SCRIPT_DIR/$f" \
            "$SCRIPT_DIR/$f.c" \
            "$FOO2ZJS_DIR/jbig.c" "$FOO2ZJS_DIR/jbig_ar.c" \
            -I"$FOO2ZJS_DIR" -lcups -lcupsimage -Wall -O2
    fi
    echo "  $f: OK"
done

# Step 2: Generate firmware files
echo "[2/5] Preparing firmware..."
if [ ! -f "$FOO2ZJS_DIR/arm2hpdl" ]; then
    clang -o "$FOO2ZJS_DIR/arm2hpdl" "$FOO2ZJS_DIR/arm2hpdl.c" -I"$FOO2ZJS_DIR" -Wall -O2
fi
for img in "$FOO2ZJS_DIR"/*.img; do
    dl="${img%.img}.dl"
    if [ ! -f "$dl" ]; then
        "$FOO2ZJS_DIR/arm2hpdl" "$img" > "$dl"
    fi
    name=$(basename "$dl")
    echo "  $name: $(wc -c < "$dl" | tr -d ' ') bytes"
done

# Step 3: Install filters
echo "[3/5] Installing CUPS filters..."
mkdir -p "$FILTER_DIR"
for f in $FILTERS; do
    cp "$SCRIPT_DIR/$f" "$FILTER_DIR/$f"
    chmod 755 "$FILTER_DIR/$f"
    codesign --force --sign - "$FILTER_DIR/$f" 2>/dev/null || true
    echo "  $f → $FILTER_DIR/"
done

# Step 4: Install PPDs
echo "[4/5] Installing PPDs..."
mkdir -p "$PPD_DIR"
count=0
for ppd in "$SCRIPT_DIR/PPD"/*.ppd; do
    cp "$ppd" "$PPD_DIR/"
    count=$((count + 1))
done
echo "  Installed $count PPD files to $PPD_DIR/"

# Step 5: Install firmware and ICM profiles
echo "[5/5] Installing firmware and color profiles..."
mkdir -p "$FIRMWARE_DIR"
for dl in "$FOO2ZJS_DIR"/*.dl; do
    cp "$dl" "$FIRMWARE_DIR/"
done
echo "  Firmware → $FIRMWARE_DIR/"

mkdir -p "$ICM_DIR"
for icm in "$FOO2ZJS_DIR"/*.icm; do
    [ -f "$icm" ] && cp "$icm" "$ICM_DIR/"
done
echo "  ICM profiles → $ICM_DIR/"

# Restart CUPS
launchctl stop org.cups.cupsd 2>/dev/null || true
launchctl start org.cups.cupsd 2>/dev/null || true

if [ "$FIRMWARE_AUTOLOAD" -eq 1 ]; then
    echo ""
    echo "Installing the optional HP LaserJet 1018 firmware autoloader..."
    "$SCRIPT_DIR/install-firmware-loader.sh"
fi

echo ""
echo "=== Installation complete! ==="
echo ""
echo "Installed 9 filters covering 89 printer models."
echo ""
echo "Next steps:"
echo "  1. Connect your printer via USB"
echo "  2. Add it in System Settings > Printers & Scanners"
echo "  3. Select the matching PPD as the driver"
echo ""
echo "For printers requiring firmware (HP LaserJet 1000/1005/1018/1020/P1005/P1006/P1505):"
echo "  lp -oraw $FIRMWARE_DIR/<firmware>.dl"
if [ "$FIRMWARE_AUTOLOAD" -eq 0 ]; then
    echo ""
    echo "HP LaserJet 1018 automatic firmware loading is available with:"
    echo "  sudo $SCRIPT_DIR/install-firmware-loader.sh"
fi
echo ""
