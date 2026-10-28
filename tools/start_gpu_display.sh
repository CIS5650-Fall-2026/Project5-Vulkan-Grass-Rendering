#!/usr/bin/env bash
# Start a local NVIDIA X11 display on a GPU server without a desktop session.
set -euo pipefail
if [[ ${EUID} -ne 0 ]]; then
    printf '%s\n' 'Run with sudo after installing the NVIDIA graphics driver and rebooting.' >&2
    exit 1
fi
if DISPLAY=:0 xrandr --current >/dev/null 2>&1; then
    printf '%s\n' 'Display :0 is already available.'
    exit 0
fi
grass_pci=$(nvidia-smi --query-gpu=pci.bus_id --format=csv,noheader | head -n 1)
grass_bus_id=$(python3 - "${grass_pci}" <<'PY'
import sys
domain, bus, device_function = sys.argv[1].strip().split(":")
device, function = device_function.split(".")
if int(domain, 16) != 0:
    raise SystemExit("Configure the Xorg BusID manually for a nonzero PCI domain")
print(f"PCI:{int(bus, 16)}:{int(device, 16)}:{int(function, 16)}")
PY
)
grass_config=$(mktemp /tmp/grass-xorg-XXXXXX.conf)
cat > "${grass_config}" <<EOF
Section "ServerLayout"
    Identifier "GrassLayout"
    Screen 0 "GrassScreen"
EndSection
Section "Device"
    Identifier "GrassGPU"
    Driver "nvidia"
    BusID "${grass_bus_id}"
    Option "AllowEmptyInitialConfiguration" "True"
EndSection
Section "Screen"
    Identifier "GrassScreen"
    Device "GrassGPU"
    DefaultDepth 24
    SubSection "Display"
        Depth 24
        Virtual 1920 1080
    EndSubSection
EndSection
EOF
nohup /usr/lib/xorg/Xorg :0 -ac -noreset -nolisten tcp -config "${grass_config}" \
    > /var/log/grass-xorg-launch.log 2>&1 < /dev/null &
for grass_attempt in {1..50}; do
    if DISPLAY=:0 xrandr --current >/dev/null 2>&1; then
        printf '%s\n' 'NVIDIA display ready. Run: export DISPLAY=:0'
        exit 0
    fi
    sleep 0.2
done
printf '%s\n' 'Xorg did not become ready. See /var/log/grass-xorg-launch.log and /var/log/Xorg.0.log.' >&2
exit 1
