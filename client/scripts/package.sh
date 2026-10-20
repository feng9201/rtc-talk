#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
: "${RK3576_HOST:?请设置 RK3576_HOST}"
BUILD="$ROOT/build/rk3576"
DEST="$BUILD/package"
cmake --install "$BUILD" --prefix "$DEST"
mkdir -p "$DEST/debug"
OBJCOPY="$RK3576_HOST/bin/aarch64-buildroot-linux-gnu-objcopy"
STRIP="$RK3576_HOST/bin/aarch64-buildroot-linux-gnu-strip"
for file in lib/libtalk_core.so bin/talk-cli bin/talk-qt; do
  name=$(basename "$file")
  "$OBJCOPY" --only-keep-debug "$DEST/$file" "$DEST/debug/$name.debug"
  "$STRIP" --strip-unneeded "$DEST/$file"
  "$OBJCOPY" --add-gnu-debuglink="$DEST/debug/$name.debug" "$DEST/$file"
done
cp "$ROOT/scripts/run-qt.sh" "$DEST/bin/run-qt.sh"
cp "$ROOT/include/talk/talk.h" "$DEST/include/talk/talk.h"
cp "$ROOT/vcpkg.json" "$DEST/dependencies.json"
{
  printf 'client_version=0.1.0\nbuild_utc=%s\n' "$(date -u +%FT%TZ)"
  printf 'git_revision=%s\n' "$(git -C "$ROOT" rev-parse HEAD)"
  printf 'git_status=%s\n' "$(test -z "$(git -C "$ROOT" status --porcelain)" && printf clean || printf dirty)"
  printf 'target=aarch64-linux-gnu\naudio_default=none\n'
} > "$DEST/manifest.txt"
(cd "$DEST" && sha256sum lib/libtalk_core.so bin/talk-cli bin/talk-qt > SHA256SUMS)
printf '发布包：%s\n' "$DEST"
