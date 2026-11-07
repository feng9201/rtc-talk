#!/usr/bin/env bash
set -euo pipefail
: "${VCPKG_ROOT:?Set VCPKG_ROOT}"
rtc_client_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$rtc_client_dir"
rtc_preset="${1:-host-net}"
case "$rtc_preset" in
  host-net|host-net-asan) ;;
  *) echo 'Expected host-net or host-net-asan' >&2; exit 2 ;;
esac
cmake --preset "$rtc_preset"
cmake --build --preset "$rtc_preset"
ctest --preset "$rtc_preset"
