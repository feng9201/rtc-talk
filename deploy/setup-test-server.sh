#!/usr/bin/env bash
# 测试机初始化（Ubuntu 24.04），可重复执行。用 root 运行：
#   sudo PUBLIC_IP=47.117.159.193 bash deploy/setup-test-server.sh
# 需要与本脚本同目录的 systemd/、coturn/ 模板。
# SSH 加固（关闭密码登录、root 登录，装 fail2ban）单独用 --harden-ssh 执行，执行前确认可用密钥登录。
set -euo pipefail
export LC_ALL=C.UTF-8

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PUBLIC_IP="${PUBLIC_IP:?需要设置 PUBLIC_IP}"
PRIVATE_IP="${PRIVATE_IP:-$(ip -4 route get 1.1.1.1 | sed -n 's/.* src \([0-9.]*\).*/\1/p')}"
NODE_VERSION="${NODE_VERSION:-22.23.3}"
NODE_MIRROR="${NODE_MIRROR:-https://npmmirror.com/mirrors/node}"
SERVICE_USER=talk
ENV_FILE=/etc/talk/talk-server.env

log() { echo -e "\n==> $*"; }

[[ $EUID -eq 0 ]] || { echo "需要 root" >&2; exit 1; }
[[ -n "$PRIVATE_IP" ]] || { echo "无法探测内网地址，请设置 PRIVATE_IP" >&2; exit 1; }

harden_ssh() {
  local keys="/home/$SERVICE_USER/.ssh/authorized_keys"
  [[ -s "$keys" ]] || { echo "$keys 为空，拒绝关闭密码登录" >&2; exit 1; }
  log "SSH 加固：关闭密码登录与 root 登录，安装 fail2ban"
  cat >/etc/ssh/sshd_config.d/90-talk.conf <<'EOF'
PasswordAuthentication no
KbdInteractiveAuthentication no
PermitRootLogin no
EOF
  sshd -t
  systemctl reload ssh
  DEBIAN_FRONTEND=noninteractive apt-get install -y fail2ban
  systemctl enable --now fail2ban
}

if [[ "${1:-}" == "--harden-ssh" ]]; then
  harden_ssh
  exit 0
fi

log "基础软件包"
apt-get update -q
DEBIAN_FRONTEND=noninteractive apt-get install -y -q coturn curl xz-utils ca-certificates jq

log "交换分区 2GB"
if ! swapon --show=NAME --noheadings | grep -qx /swapfile; then
  [[ -f /swapfile ]] || { fallocate -l 2G /swapfile; chmod 600 /swapfile; mkswap /swapfile; }
  swapon /swapfile
fi
grep -q '^/swapfile ' /etc/fstab || echo '/swapfile none swap sw 0 0' >>/etc/fstab

log "内核参数"
# 阿里云镜像在 /etc/sysctl.conf（即 99-sysctl.conf）里设了 vm.swappiness=0，文件名要排在它之后
rm -f /etc/sysctl.d/90-talk.conf
cat >/etc/sysctl.d/zz-talk.conf <<'EOF'
vm.swappiness = 10
net.core.rmem_max = 8388608
net.core.wmem_max = 8388608
net.core.rmem_default = 1048576
net.core.wmem_default = 1048576
fs.file-max = 1048576
EOF
sysctl -q --system
# sysctl --system 最后还会再读一次 /etc/sysctl.conf
sysctl -q -p /etc/sysctl.d/zz-talk.conf

log "Node $NODE_VERSION -> /opt/node"
if [[ "$(/opt/node/bin/node --version 2>/dev/null)" != "v$NODE_VERSION" ]]; then
  tmp="$(mktemp -d)"
  tarball="node-v${NODE_VERSION}-linux-x64.tar.xz"
  curl -fsSL -o "$tmp/$tarball" "$NODE_MIRROR/v$NODE_VERSION/$tarball"
  curl -fsSL -o "$tmp/SHASUMS256.txt" "$NODE_MIRROR/v$NODE_VERSION/SHASUMS256.txt"
  (cd "$tmp" && grep " ${tarball}\$" SHASUMS256.txt | sha256sum -c -)
  rm -rf /opt/node.new && mkdir -p /opt/node.new
  tar -xJf "$tmp/$tarball" -C /opt/node.new --strip-components=1 --no-same-owner
  rm -rf /opt/node && mv /opt/node.new /opt/node
  rm -rf "$tmp"
fi
ln -sf /opt/node/bin/node /usr/local/bin/node
/opt/node/bin/node --version

log "目录与配置"
id "$SERVICE_USER" >/dev/null
install -d -o "$SERVICE_USER" -g "$SERVICE_USER" -m 755 /opt/talk /opt/talk/releases
install -d -o root -g "$SERVICE_USER" -m 750 /etc/talk

# 密钥只在首次生成，之后沿用
existing() { [[ -f "$ENV_FILE" ]] && sed -n "s/^$1=//p" "$ENV_FILE" || true; }
TURN_SECRET="$(existing TALK_TURN_SECRET)"
[[ -n "$TURN_SECRET" ]] || TURN_SECRET="$(openssl rand -hex 32)"
JWT_SECRET="$(existing TALK_JWT_SECRET)"
[[ -n "$JWT_SECRET" ]] || JWT_SECRET="$(openssl rand -hex 32)"
cat >"$ENV_FILE" <<EOF
# 由 setup-test-server.sh 生成
# 测试期 TALK_ENV=dev：启用 /dev/token 开发 token
TALK_ENV=dev
TALK_LOG_LEVEL=info
TALK_HTTP_HOST=0.0.0.0
TALK_HTTP_PORT=8080
TALK_NUM_WORKERS=1
TALK_RTC_LISTEN_IP=0.0.0.0
TALK_RTC_ANNOUNCED_ADDRESS=$PUBLIC_IP
TALK_RTC_PORT=44444
TALK_TURN_URLS=turn:$PUBLIC_IP:3478?transport=udp,turn:$PUBLIC_IP:3478?transport=tcp
TALK_TURN_SECRET=$TURN_SECRET
TALK_JWT_SECRET=$JWT_SECRET
EOF
chown root:"$SERVICE_USER" "$ENV_FILE"
chmod 640 "$ENV_FILE"

log "coturn"
sed -e "s/@PUBLIC_IP@/$PUBLIC_IP/g" -e "s/@PRIVATE_IP@/$PRIVATE_IP/g" -e "s/@TURN_SECRET@/$TURN_SECRET/g" \
  "$HERE/coturn/turnserver.conf.tmpl" >/etc/turnserver.conf
chown root:turnserver /etc/turnserver.conf
chmod 640 /etc/turnserver.conf
systemctl enable -q coturn
systemctl restart coturn

log "talk-server systemd 服务"
install -m 644 "$HERE/systemd/talk-server.service" /etc/systemd/system/talk-server.service
systemctl daemon-reload
systemctl enable -q talk-server

# deploy.sh 以 talk 用户执行，只放行对 talk-server 这一个服务的启停；看日志靠 systemd-journal 组
cat >/etc/sudoers.d/talk-deploy <<'EOF'
Cmnd_Alias TALK_SVC = /usr/bin/systemctl restart talk-server, /usr/bin/systemctl stop talk-server, \
                      /usr/bin/systemctl start talk-server, /usr/bin/systemctl status talk-server, \
                      /usr/bin/systemctl status talk-server *
talk ALL=(root) NOPASSWD: TALK_SVC
EOF
chmod 440 /etc/sudoers.d/talk-deploy
visudo -cq
usermod -aG systemd-journal "$SERVICE_USER"

log "完成"
echo "PUBLIC_IP=$PUBLIC_IP PRIVATE_IP=$PRIVATE_IP"
swapon --show
sysctl vm.swappiness net.core.rmem_max
systemctl is-active coturn
[[ -e /opt/talk/current ]] || echo "talk-server 尚未部署（/opt/talk/current 不存在），用 deploy/deploy.sh 发布"
