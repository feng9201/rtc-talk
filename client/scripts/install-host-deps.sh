#!/usr/bin/env bash
set -euo pipefail
: "${VCPKG_ROOT:?Set VCPKG_ROOT to the approved vcpkg checkout}"
rtc_client_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
"$VCPKG_ROOT/vcpkg" install --triplet=x64-linux --host-triplet=x64-linux \
  --x-manifest-root="$rtc_client_dir" \
  --x-install-root="$rtc_client_dir/build/host-net/vcpkg_installed" \
  --x-buildtrees-root="$rtc_client_dir/build/host-net/vcpkg-buildtrees" \
  --x-packages-root="$rtc_client_dir/build/host-net/vcpkg-packages" \
  --downloads-root="$rtc_client_dir/build/vcpkg-downloads"
