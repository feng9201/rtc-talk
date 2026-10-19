#!/usr/bin/env bash
# 打部署包（在开发容器内执行）：deploy/docker/dev.sh exec bash deploy/package.sh
# 产物：release/talk-server-<rev>.tar.gz，含 server/dist、生产依赖、bin/mediasoup-worker
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

REV="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
if [[ -n "$(git status --porcelain 2>/dev/null)" ]]; then REV="$REV-dirty"; fi
STAGE="$ROOT/release/stage"
OUT="$ROOT/release/talk-server-$REV.tar.gz"

echo "==> 构建 $REV"
npm ci --no-fund --no-audit
npm run -s typecheck
npm test -s
npm run -s build

echo "==> 生产依赖"
rm -rf "$STAGE" && mkdir -p "$STAGE/server" "$STAGE/tools/bot" "$STAGE/scripts"
cp package.json package-lock.json "$STAGE/"
cp server/package.json "$STAGE/server/"
# npm ci 要求所有 workspace 都在，bot 只带 package.json（依赖 ws 与 server 共用）
cp tools/bot/package.json "$STAGE/tools/bot/"
cp scripts/fetch-mediasoup-worker.mjs "$STAGE/scripts/"
# 预先放入已校验的 worker，postinstall 发现版本一致就不再下载
mkdir -p "$STAGE/.cache" && cp -a .cache/mediasoup-worker "$STAGE/.cache/"
(cd "$STAGE" && MEDIASOUP_WORKER_BIN=/dev/null npm ci --omit=dev --no-fund --no-audit)

cp -a server/dist "$STAGE/server/"
mkdir -p "$STAGE/bin"
mv "$STAGE/.cache/mediasoup-worker/mediasoup-worker" "$STAGE/.cache/mediasoup-worker/VERSION" "$STAGE/bin/"
rm -rf "$STAGE/.cache" "$STAGE/scripts" "$STAGE/node_modules/mediasoup/worker"
find "$STAGE/node_modules" "$STAGE/server" -type d -empty -delete
echo "TALK_BUILD_REV=$REV" >"$STAGE/release.env"

tar -czf "$OUT" -C "$STAGE" .
rm -rf "$STAGE"
echo "==> $OUT ($(du -h "$OUT" | cut -f1))"
