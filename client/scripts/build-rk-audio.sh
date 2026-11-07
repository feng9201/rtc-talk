#!/usr/bin/env bash
set -euo pipefail
: "${VCPKG_ROOT:?Set VCPKG_ROOT}"
: "${RK3576_HOST:?Set RK3576_HOST}"
: "${WEBRTC_SOURCE_DIR:?Set WEBRTC_SOURCE_DIR}"
: "${WEBRTC_BUILD_DIR:?Set WEBRTC_BUILD_DIR}"
rtc_client_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$rtc_client_dir"
# The audio preset reuses this product's OWN public dependency installation.
# No additional packages or private-library fetch is needed for media.
rtc_audio_preset="${1:-rk3576-audio}"
case "$rtc_audio_preset" in rk3576-audio|rk3576-audio-dev) ;; *) exit 2 ;; esac
cmake --preset "$rtc_audio_preset"
cmake --build --preset "$rtc_audio_preset"
