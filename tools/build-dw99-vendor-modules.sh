#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

if (( $# != 2 )); then
    echo "Usage: $0 <kernel-build-directory> <module-output-directory>" >&2
    exit 2
fi

source_tree=$(cd -- "$(dirname -- "$0")/.." && pwd -P)
kernel_build=$(realpath -- "$1")
output=$(realpath -m -- "$2")

if [[ ! -s "$kernel_build/Module.symvers" ||
      ! -s "$kernel_build/include/config/kernel.release" ]]; then
    echo "Build Image and modules in the target kernel build directory first." >&2
    exit 1
fi

kernel_release=$(<"$kernel_build/include/config/kernel.release")
radio_source="$source_tree/drivers/misc/sprdwcn_bsp/sipc_modules"
mkdir -p -- "$output"

build_module()
{
    local name=$1 source=$2 module_dir="$output/build-$1" vermagic

    make -C "$kernel_build" M="$source" MO="$module_dir" \
        ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}" \
        BSP_BOARD_UNISOC_WCN_SOCKET=sipc -j"${JOBS:-$(nproc)}" modules

    vermagic=$(LC_ALL=C readelf -p .modinfo "$module_dir/$name.ko" |
        sed -nE 's/^.*vermagic=([^ ]+).*$/\1/p')
    if [[ "$vermagic" != "$kernel_release" ]]; then
        echo "$name.ko has unexpected kernel version: $vermagic" >&2
        exit 1
    fi
}

build_module sprdbt_tty "$radio_source/bluetooth/driver"
build_module sprd_fm "$radio_source/fm/driver"
build_module mali "$source_tree/drivers/gpu/arm/midgard/mali"

for module in sprdbt_tty sprd_fm mali; do
    install -m 0644 -- "$output/build-$module/$module.ko" "$output/$module.ko"
done
