#!/bin/sh
# 只读诊断；不录音、不播放、不改 mixer、不停止服务。
set -u
printf '\n[系统]\n'
uname -a
printf '\n[声卡]\n'
cat /proc/asound/cards /proc/asound/pcm 2>/dev/null
printf '\n[PCM 占用]\n'
for status in /proc/asound/card*/pcm*/sub*/status; do
  [ -r "$status" ] || continue
  printf '%s\n' "$status"
  cat "$status"
done
printf '\n[音频与显示进程]\n'
ps | grep -E 'pulseaudio|pipewire|weston|talk-' || true
printf '\n[ALSA 逻辑设备]\n'
command -v aplay >/dev/null && aplay -L
command -v arecord >/dev/null && arecord -L
printf '\n[mixer 当前值]\n'
if command -v amixer >/dev/null; then
  for card in 0 1 2 3; do amixer -c "$card" scontents 2>/dev/null; done
fi
printf '\n[显示连接]\n'
for status in /sys/class/drm/card*-*/status; do
  [ -r "$status" ] || continue
  printf '%s: ' "$status"; cat "$status"
done
printf '\n[输入设备]\n'
cat /proc/bus/input/devices 2>/dev/null
