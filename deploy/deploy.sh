#!/usr/bin/env bash
# 发布到测试机（在开发机宿主上执行）：
#   deploy/deploy.sh                 打包并发布当前代码
#   deploy/deploy.sh <包.tar.gz>     发布已有的包
#   deploy/deploy.sh --rollback      切回上一个版本
#   deploy/deploy.sh --status        查看当前版本与健康状态
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOST="${TALK_DEPLOY_HOST:-talk@47.117.159.193}"
PUBLIC_URL="${TALK_PUBLIC_URL:-http://47.117.159.193:8080}"
KEEP="${TALK_KEEP_RELEASES:-5}"
SSH=(ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST")

remote() { "${SSH[@]}" bash -s <<<"$REMOTE_LIB$1"; }

# 远端执行：切换 current 软链、重启、健康检查，失败自动回滚
REMOTE_LIB='
set -euo pipefail
export LC_ALL=C.UTF-8
BASE=/opt/talk
health() {
  for _ in $(seq 1 20); do
    curl -fsS --max-time 2 http://127.0.0.1:8080/healthz 2>/dev/null && echo && return 0
    sleep 0.5
  done
  return 1
}
switch_to() {
  local target="$1"
  ln -sfn "$target" "$BASE/current.tmp" && mv -Tf "$BASE/current.tmp" "$BASE/current"
  sudo -n systemctl restart talk-server
}
'

status() {
  remote '
    echo "current:  $(readlink $BASE/current || echo -)"
    echo "previous: $(readlink $BASE/previous || echo -)"
    ls -1 $BASE/releases
    health || echo "unhealthy"
  '
}

rollback() {
  remote '
    prev="$(readlink $BASE/previous || true)"
    [[ -n "$prev" && -d "$prev" ]] || { echo "没有可回滚的版本" >&2; exit 1; }
    cur="$(readlink $BASE/current)"
    switch_to "$prev"
    ln -sfn "$cur" $BASE/previous
    echo "rollback -> $prev"
    health
  '
}

deploy() {
  local pkg="${1:-}"
  if [[ -z "$pkg" ]]; then
    "$ROOT/deploy/docker/dev.sh" exec bash deploy/package.sh
    pkg="$(ls -t "$ROOT"/release/talk-server-*.tar.gz | head -1)"
  fi
  [[ -f "$pkg" ]] || { echo "找不到部署包 $pkg" >&2; exit 1; }
  local name rel
  name="$(basename "$pkg" .tar.gz)"
  rel="$(date +%Y%m%d-%H%M%S)-${name#talk-server-}"

  echo "==> 上传 $pkg -> $HOST:/opt/talk/releases/$rel"
  scp -q "$pkg" "$HOST:/tmp/$name.tar.gz"
  remote '
    rel='"$rel"'; name='"$name"'
    mkdir -p "$BASE/releases/$rel"
    tar --no-same-owner -xzf "/tmp/$name.tar.gz" -C "$BASE/releases/$rel"
    rm -f "/tmp/$name.tar.gz"
    cur="$(readlink $BASE/current || true)"
    switch_to "$BASE/releases/$rel"
    if health; then
      [[ -n "$cur" ]] && ln -sfn "$cur" $BASE/previous
      echo "deployed $rel"
    else
      echo "健康检查失败，最近日志：" >&2
      journalctl -u talk-server --no-pager -n 30 >&2 || true
      if [[ -n "$cur" ]]; then
        echo "回滚到 $cur" >&2
        switch_to "$cur"
        health || true
      fi
      exit 1
    fi
    # 只保留最近 '"$KEEP"' 个版本，current / previous 不删
    keep_cur="$(readlink -f $BASE/current)"; keep_prev="$(readlink -f $BASE/previous || true)"
    ls -1d $BASE/releases/*/ | sed "s#/\$##" | sort -r | tail -n +'"$((KEEP + 1))"' | while read -r d; do
      [[ "$d" == "$keep_cur" || "$d" == "$keep_prev" ]] || rm -rf "$d"
    done
  '
  echo "==> 公网检查 $PUBLIC_URL/healthz"
  curl -fsS --noproxy '*' --max-time 5 "$PUBLIC_URL/healthz" && echo
}

case "${1:-}" in
  --status) status ;;
  --rollback) rollback ;;
  -h | --help) sed -n '2,6p' "$0" ;;
  *) deploy "${1:-}" ;;
esac
