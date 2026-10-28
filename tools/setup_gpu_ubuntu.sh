#!/usr/bin/env bash
# Development-machine setup for a fresh Ubuntu 24.04 x86_64 NVIDIA GPU host.
set -euo pipefail
if [[ ${EUID} -ne 0 ]]; then
    printf '%s\n' 'Run with sudo on the development GPU host.' >&2
    exit 1
fi
export DEBIAN_FRONTEND=noninteractive
grass_sdk_version=1.4.363.0
apt-get update
apt-get install -y build-essential cmake ninja-build git python3 curl ca-certificates xz-utils \
    linux-headers-"$(uname -r)" pkg-config libx11-dev libxrandr-dev libxinerama-dev \
    libxcursor-dev libxi-dev libxkbcommon-dev libxcb1-dev libx11-xcb-dev \
    xserver-xorg-core xinit xauth x11-xserver-utils xfonts-base vulkan-tools ffmpeg xdotool
curl --fail --location --retry 3 \
    https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/cuda-keyring_1.1-1_all.deb \
    --output /var/tmp/grass-cuda-keyring.deb
dpkg -i /var/tmp/grass-cuda-keyring.deb
apt-get update
# The full driver includes Vulkan and X11; compute-only packages are insufficient.
apt-get install -y nvidia-open
mkdir -p /opt/VulkanSDK
curl --fail --location --retry 3 \
    "https://sdk.lunarg.com/sdk/download/${grass_sdk_version}/linux/vulkansdk-linux-x86_64-${grass_sdk_version}.tar.xz" \
    --output /var/tmp/grass-vulkan-sdk.tar.xz
sha256sum /var/tmp/grass-vulkan-sdk.tar.xz > /opt/VulkanSDK/download.sha256
tar -xJf /var/tmp/grass-vulkan-sdk.tar.xz -C /opt/VulkanSDK
printf 'source /opt/VulkanSDK/%s/setup-env.sh\n' "${grass_sdk_version}" > /etc/profile.d/grass-vulkan-sdk.sh
printf '%s\n' 'SDK and driver installed. Reboot before starting the NVIDIA Xorg display.'
