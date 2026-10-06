#!/bin/bash
# Install / reload oaf.ko on the PVE host and set it up for boot.
# Must run as root ON the PVE host, after build.sh succeeded.
set -e

KVER="$(uname -r)"
SRC_DIR="$(cd "$(dirname "$0")/.." && pwd)/oaf/src"
KO="$SRC_DIR/oaf.ko"

[ -f "$KO" ] || { echo "ERROR: $KO not found, run pve/build.sh first"; exit 1; }

if lsmod | grep -q '^oaf '; then
    echo "==> Unloading previous oaf module..."
    rmmod oaf || { echo "ERROR: cannot rmmod oaf (traffic in use? restart with pve/uninstall.sh first)"; exit 1; }
fi

echo "==> Loading dependencies..."
modprobe nf_conntrack 2>/dev/null || insmod "$(modinfo -n nf_conntrack 2>/dev/null || true)" 2>/dev/null || true
modprobe nf_reject_ipv4 2>/dev/null || true

echo "==> Installing module into /lib/modules/$KVER/extra ..."
mkdir -p "/lib/modules/$KVER/extra"
cp "$KO" "/lib/modules/$KVER/extra/oaf.ko"
depmod -a "$KVER"

echo "==> Loading oaf..."
modprobe oaf || insmod "$KO"

# load on every host boot (nf_conntrack/nf_reject_ipv4 are auto pulled as deps)
printf 'oaf\n' > /etc/modules-load.d/oaf.conf

MAJOR="$(awk '$2=="fwx"{print $1}' /proc/devices)"
if [ -z "$MAJOR" ]; then
    echo "ERROR: /dev/fwx char device not registered, check: dmesg | grep fwx"
    exit 1
fi

echo
echo "=============================================================="
echo " oaf loaded. /dev/fwx char device MAJOR = $MAJOR"
echo
echo " Add to /etc/pve/lxc/<vmid>.conf of the ImmortalWrt container:"
echo
echo "   lxc.cgroup2.devices.allow: c $MAJOR:0 rwm"
echo "   lxc.mount.entry: /dev/fwx dev/fwx none bind,optional,create=file 0 0"
echo "   lxc.mount.entry: /proc/sys/fwx proc/sys/fwx none bind,optional,create=dir 0 0"
echo
echo " Then (re)start the container. Verify with:"
echo "   host: cat /proc/sys/fwx/version ; cat /proc/net/af_conn"
echo "   container: ls /proc/net/af_client ; cat /proc/sys/fwx/feature_count"
echo "=============================================================="
