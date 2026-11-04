# 视频黑屏诊断日志

日志位置：`%LOCALAPPDATA%\rtc-talk-win\logs\rtcwin.log`，轮转后为
`rtcwin.1.log`。诊断双方均使用包含本次日志改动的构建，从入房前开始记录，
复现后各保留当前及轮转日志，按时间、generation、pc、mid 对照。

## 本次现场证据

2026-11-02 vge 提供的片段覆盖 21:34:56—21:35:11：camera_frames 从 83
增加到 482，video_sent 从 340 增加到 2493，screen_sender=false，
SFU 收发连接均为 connected。这证明该片段内摄像头持续送帧并产生上行视频 RTP，
不能据此证明 SFU 已向其他成员转发，亦不能判定为 vge 没有发视频。
此前接收电脑的片段结束于 21:30，与本片段不同时，不能直接作为同一时刻的对照。
现有片段不足以确认根因。

## 新增字段与判读

- `sfu subscribe` / `sfu consumer`：订阅协商结果及 mid → uid/kind/source 映射。
- `media producer`：服务端通知的 uid、source、paused；不是实际收包证明。
- `media track`：媒体轨道事件的 pc、mid、uid、kind、source。
- `video rtp`：每个 pc / direction / mid / ssrc 的累计 packets、bytes，
  codec、implementation、frames、key_frames、frames_received 和 nack/pli/fir。
  out 的 frames/key_frames 是编码计数，in 是解码计数；frames_received
  仅用于接收方向。`-1` 表示 SDK 未提供该帧计数。反馈计数：out 为收到，in 为发出。
- `video sink`：轨道启用状态、当前 uid/source 映射、解码后抵达 sink 的累计帧数，
  以及成功转为 I420、准备调用回调的累计帧数。转换成功不等于 UI 已绘制。

RTP 和 sink 详情随现有 media.stats 轮询记录（桌面通常约 5 秒一次），不逐帧写日志。
计数为累计值，连接/轨道重建可能归零；sink 计数还会在替换 sink 时归零，
需与协商日志一起查看。没有 inbound RTP 项不等于零帧已被解码，可能尚未收到该路 RTP；
结合总 video_recv、consumer 映射及 sink 记录判断。

1. 发送端 packets/编码帧增长，接收端对应 consumer 无 RTP 增长：检查 SFU
   producer/consumer、暂停状态及转发链路；通常需要 SFU 日志继续定位。
2. 接收 packets 增长但解码帧不增长：检查编解码器、关键帧和反馈请求。
3. 解码增长但 sink 不增长：检查轨道启用、绑定与重新协商。
4. sink/转换计数增长仍无画面：检查 uid/source 映射以及 UI 接收、绘制链路。

不记录完整信令 payload、Token、SDP、ICE 凭证或候选地址。此次只补诊断，
不改变采集、编解码、协商或 UI 行为。
