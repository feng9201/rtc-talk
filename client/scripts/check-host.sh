#!/usr/bin/env bash
set -euo pipefail
rtc_client_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$rtc_client_dir"
rtc_preset="${1:-host}"
case "$rtc_preset" in
  host|host-asan|host-tsan) ;;
  *) echo 'Expected host, host-asan or host-tsan' >&2; exit 2 ;;
esac
cmake --preset "$rtc_preset"
cmake --build --preset "$rtc_preset"
ctest --preset "$rtc_preset"
