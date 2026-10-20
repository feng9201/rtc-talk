#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
PRESET=${1:-host-test}
case "$PRESET" in
  host-test) TRIPLET=x64-linux ;;
  rk3576) TRIPLET=arm64-rk3576 ;;
  *) printf '仅支持 host-test 或 rk3576\n' >&2; exit 2 ;;
esac
: "${VCPKG_ROOT:?请设置 VCPKG_ROOT}"
if [[ "$PRESET" == rk3576 ]]; then
  : "${RK3576_HOST:?请设置 RK3576_HOST}"
  : "${WEBRTC_SOURCE_DIR:?请设置 WEBRTC_SOURCE_DIR}"
  : "${WEBRTC_BUILD_DIR:?请设置 WEBRTC_BUILD_DIR}"
fi
# 所有安装、下载和构建缓存均隔离到当前工程，不修改共用 port。
export VCPKG_BINARY_SOURCES=clear
"$VCPKG_ROOT/vcpkg" install --triplet="$TRIPLET" --host-triplet=x64-linux \
  --overlay-triplets="$ROOT/cmake/triplets" --x-manifest-root="$ROOT" \
  --x-install-root="$ROOT/build/$PRESET/vcpkg_installed" \
  --x-buildtrees-root="$ROOT/build/$PRESET/vcpkg-buildtrees" \
  --x-packages-root="$ROOT/build/$PRESET/vcpkg-packages" \
  --downloads-root="$ROOT/build/vcpkg-downloads"
cmake --fresh -S "$ROOT" --preset "$PRESET"
cmake --build "$ROOT/build/$PRESET" --parallel "${JOBS:-4}"
if [[ "$PRESET" == host-test ]]; then
  ctest --test-dir "$ROOT/build/$PRESET" --output-on-failure
fi
