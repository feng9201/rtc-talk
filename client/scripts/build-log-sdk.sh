#!/usr/bin/env bash
set -euo pipefail
: "${VCPKG_ROOT:?Set VCPKG_ROOT}"
rtc_client_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
rtc_platform="${1:-host}"
rtc_log_source="${LOGCPP_SOURCE_DIR:-$rtc_client_dir/../thirdPart/log-cpp}"
rtc_log_revision=d8062b7a74c86028a3a927e76e590f986ce7267b
if [[ "$(git -C "$rtc_log_source" rev-parse HEAD)" != "$rtc_log_revision" ]]; then
  echo 'User-provided log-cpp revision does not match the reviewed revision' >&2
  exit 2
fi
git -C "$rtc_log_source" diff --ignore-space-at-eol --quiet
case "$rtc_platform" in
  host) rtc_triplet=x64-linux; rtc_chainload=() ;;
  rk3576)
    : "${RK3576_HOST:?Set RK3576_HOST}"
    rtc_triplet=arm64-rk3576
    rtc_chainload=("-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$rtc_client_dir/cmake/toolchain-rk3576.cmake") ;;
  *) echo 'Expected host or rk3576' >&2; exit 2 ;;
esac
rtc_build="$rtc_client_dir/build/log-sdk-$rtc_platform"
"$VCPKG_ROOT/vcpkg" install --triplet="$rtc_triplet" --host-triplet=x64-linux \
  --overlay-triplets="$rtc_client_dir/cmake/triplets" \
  --x-manifest-root="$rtc_client_dir/adapters/log-cpp" \
  --x-install-root="$rtc_build/vcpkg_installed" \
  --x-buildtrees-root="$rtc_build/vcpkg-buildtrees" \
  --x-packages-root="$rtc_build/vcpkg-packages" \
  --downloads-root="$rtc_client_dir/build/vcpkg-downloads"
cmake -S "$rtc_client_dir/adapters/log-cpp" -B "$rtc_build" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_MANIFEST_INSTALL=OFF -DVCPKG_TARGET_TRIPLET="$rtc_triplet" \
  -DVCPKG_INSTALLED_DIR="$rtc_build/vcpkg_installed" -DVCPKG_OVERLAY_TRIPLETS="$rtc_client_dir/cmake/triplets" \
  -DLOGCPP_SOURCE_DIR="$rtc_log_source" -DRTC_LOG_SOURCE_REVISION="$rtc_log_revision" "${rtc_chainload[@]}"
cmake --build "$rtc_build" --parallel 2
cmake --install "$rtc_build" --component rtc_log_sdk --prefix "$rtc_client_dir/third_party/log-cpp"
