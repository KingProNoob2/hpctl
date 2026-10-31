#!/bin/sh
# install hpctl: /usr/local/bin/hpctl + modules + systemd units
set -e
[ "$(id -u)" -eq 0 ] || { echo "install.sh: run as root" >&2; exit 1; }
PREFIX=${PREFIX:-/usr/local}

install -d "$PREFIX/bin" "$PREFIX/lib/hpctl"
install -m 0755 hpctl "$PREFIX/bin/hpctl"
install -m 0644 lib/hpstps.c lib/hpread.c lib/Makefile "$PREFIX/lib/hpctl/"

echo "building kernel modules against $(uname -r) ..."
make -C "$PREFIX/lib/hpctl" >/dev/null

install -m 0644 systemd/hpctl-profile.service systemd/hpctl-gpuclock.service /etc/systemd/system/
[ -f /etc/hpctl.profile ] || echo 1   > /etc/hpctl.profile
[ -f /etc/hpctl.gpumin ] || echo 600 > /etc/hpctl.gpumin
[ -f /etc/hpctl.igpumin ] || echo 700 > /etc/hpctl.igpumin

systemctl daemon-reload
systemctl enable --now hpctl-profile.service hpctl-gpuclock.service
echo "installed. try: hpctl profile 3"
