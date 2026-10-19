#!/usr/bin/env bash
# 开发容器管理：build | up | down | shell | exec <cmd...> | logs
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPOSE=(docker compose -f "$HERE/compose.yaml")
IMAGE=rtc-talk-dev:node22

BUILDER="${TALK_DEV_PREFIX:-wdz}-builder"

# 开发机 Docker 20.10.9 的默认 seccomp 不认识 clone3，Ubuntu 24.04（glibc 2.39）里 apt 等会失败，
# 且守护进程构建时不支持 --security-opt。改用 buildx docker-container 驱动（自带新版 runc / seccomp）。
build() {
  docker buildx inspect "$BUILDER" >/dev/null 2>&1 ||
    docker buildx create --name "$BUILDER" --driver docker-container >/dev/null
  docker buildx build --builder "$BUILDER" --load \
    -t "$IMAGE" -f "$HERE/Dockerfile.dev" "$HERE"
  docker buildx stop "$BUILDER"
}

# 当前 shell 若还没有 docker 组（远程服务端未重启），自动经 sg 执行
if ! docker info >/dev/null 2>&1 && id -nG "$USER" | grep -qw docker && [[ -z "${TALK_DEV_SG:-}" ]]; then
  exec env TALK_DEV_SG=1 sg docker -c "$(printf '%q ' "$0" "$@")"
fi

cmd="${1:-}"
shift || true
case "$cmd" in
  build) build ;;
  up)
    docker image inspect "$IMAGE" >/dev/null 2>&1 || build
    "${COMPOSE[@]}" up -d "$@"
    ;;
  down) "${COMPOSE[@]}" down "$@" ;;
  shell) "${COMPOSE[@]}" exec talk-dev bash ;;
  exec) "${COMPOSE[@]}" exec -T talk-dev "$@" ;;
  logs) "${COMPOSE[@]}" logs -f --tail=200 "$@" ;;
  *)
    echo "用法: $0 build|up|down|shell|exec <cmd...>|logs [service]" >&2
    exit 1
    ;;
esac
