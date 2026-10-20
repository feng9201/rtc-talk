#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
# 仅设置进程环境，不改系统插件、音频路由或已有显示服务。
export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-wayland}
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run}
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}
export LD_LIBRARY_PATH="$ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if [ -z "${QT_QPA_FONTDIR:-}" ] && [ -d /usr/share/fonts/source-han-sans-cn ]; then
  export QT_QPA_FONTDIR=/usr/share/fonts/source-han-sans-cn
fi
exec "$ROOT/bin/talk-qt" "$@"
