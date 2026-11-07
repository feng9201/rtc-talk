#!/usr/bin/env bash
set -euo pipefail
: "${VCPKG_ROOT:?Set VCPKG_ROOT}"
: "${RK3576_HOST:?Set RK3576_HOST}"
rtc_client_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$rtc_client_dir"
"$VCPKG_ROOT/vcpkg" install --triplet=arm64-rk3576 --host-triplet=x64-linux \
  --overlay-triplets="$rtc_client_dir/cmake/triplets" \
  --x-manifest-root="$rtc_client_dir" \
  --x-install-root="$rtc_client_dir/build/rk3576-net/vcpkg_installed" \
  --x-buildtrees-root="$rtc_client_dir/build/rk3576-net/vcpkg-buildtrees" \
  --x-packages-root="$rtc_client_dir/build/rk3576-net/vcpkg-packages" \
  --downloads-root="$rtc_client_dir/build/vcpkg-downloads"
cmake --preset rk3576-net
cmake --build --preset rk3576-net
# ARM binaries are NOT executed on the host. Board tests are separate, explicit.
