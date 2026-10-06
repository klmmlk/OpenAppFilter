#!/bin/bash
# Remove oaf from the PVE host.
set -e

KVER="$(uname -r)"

if lsmod | grep -q '^oaf '; then
    echo "==> Unloading oaf..."
    rmmod oaf || { echo "ERROR: rmmod failed; is oafd still writing? stop the container service first."; exit 1; }
fi

rm -f "/lib/modules/$KVER/extra/oaf.ko"
depmod -a "$KVER" 2>/dev/null || true
rm -f /etc/modules-load.d/oaf.conf
echo "==> oaf removed from host."
echo "NOTE: remove the 3 lxc.* lines from /etc/pve/lxc/<vmid>.conf if you no longer use the plugin."
