#!/bin/bash
# Build oaf.ko against the running Proxmox VE host kernel.
# Run this ON the PVE host (SSH), inside the source directory that
# contains oaf/ and pve/ (e.g. /root/OpenAppFilter).
set -e

KVER="${KVER:-$(uname -r)}"
SRC_DIR="$(cd "$(dirname "$0")/.." && pwd)/oaf/src"

echo "==> Target kernel : $KVER"
echo "==> Module source : $SRC_DIR"

if ! command -v make >/dev/null || ! command -v gcc >/dev/null; then
    echo "==> Installing build tools..."
    apt-get update
    apt-get install -y build-essential bc flex bison libelf-dev libssl-dev
fi

if [ ! -e "/lib/modules/$KVER/build" ]; then
    echo "==> Kernel headers for $KVER missing, installing..."
    apt-get update
    # PVE 8: pve-headers-<ver>  |  PVE 9: proxmox-headers-<ver>  | plain Debian: linux-headers-<ver>
    apt-get install -y "pve-headers-$KVER" 2>/dev/null \
        || apt-get install -y "proxmox-headers-$KVER" 2>/dev/null \
        || apt-get install -y "linux-headers-$KVER" \
        || { echo "ERROR: no headers package found for $KVER"; exit 1; }
fi

echo "==> Building..."
make -C "/lib/modules/$KVER/build" M="$SRC_DIR" modules KCFLAGS="-Wno-error" V=0

echo
echo "==> Done: $SRC_DIR/oaf.ko"
modinfo "$SRC_DIR/oaf.ko" | head -n 6 || true
