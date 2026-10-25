# RTC Talk 改动与排障记录

> **第一部分（第 1~9 节）· 音轨 source 透传改动记录**（服务端）：已回退，部分内容已被上游 `62df51b` 覆盖。
> **第二部分（2026-10-22）· 双人 SFU 对讲现场问题**（客户端）：双向无声与共享按钮置灰，已修复并实测验证。

## 第一部分 · 音轨 source 透传改动记录

> 状态：已回退（2026-10-22）
> 关联客户端提交：`97e91a4 feat(client-win): 支持运行时切换采集源（麦克风/系统音）`
> 涉及文件：`server/src/signaling.ts`、`server/src/media/media-node.ts`、`server/src/media/room-media.ts`
> 代码状态：改动已从工作区恢复；本文保留改动内容、原因与重新落地说明。
> 后续：2026-10-22 已合入 `origin/main` 的 `62df51b`（服务端 H.264 视频），上游实现已覆盖本部分大部分改动，见第 9 节。

### 1. 背景

Windows 客户端新增了运行时采集源切换（`audio.setSource`）：

- 麦克风：走系统默认通信设备；
- 系统音：走默认播放设备的 WASAPI loopback；
- 两种来源复用同一条音频 track，发布时在 `sfu.publish` 的 `tracks[]` 里标注 `source`：
  - 麦克风 → `mic`
  - 系统音 → `screen`（沿用协议中非麦克风来源的枚举值）

客户端 `TrackDescriptions`（`core/media/webrtc/webrtc_media.cpp`）负责生成该字段。

### 2. 问题

服务端没有使用客户端上报的 `tracks[].source`：

1. `Signaling.publish` 调用 `media.publish()` 时只传了 SDP，丢掉了 `tracks`；
2. `MediaNode.publish` 没有 source 参数；
3. `PeerMedia.publish` 创建 producer 时把 `appData.source` 写死为 `'mic'`；
4. `PeerMedia.openConsumer` / `negotiateOnce` 下发的 `consumers[].source` 也写死为 `'mic'`。

结果：系统音在服务端和接收端都被标记成麦克风，来源信息丢失。

### 3. 目的

- **保留元数据**：让 `source` 从发布端一路传到接收端，避免系统音被误标为麦克风；
- **协议一致性**：`talk-protocol.md` 已定义 `tracks[].source` 与 `consumers[].source`，服务端实现补齐；
- **为后续能力留接口**：远端系统音标识、共享/录制路由、诊断面板按来源区分统计；
- **低风险**：只改元数据，不改 SDP 协商、媒体转发与编解码。

需要明确：**当前客户端并不消费 `consumers[].source`**，所以这次改动对现有音频听感、静音、转发没有任何影响，属于提前打通的预留字段。

### 4. 改动内容（回退前）

#### 4.1 `server/src/signaling.ts`

`sfu.publish` 把请求中的 `tracks` 继续传给媒体层：

```diff
-    return this.opts.media.publish(s.roomId, s.uid, d.sdp);
+    return this.opts.media.publish(s.roomId, s.uid, d.sdp, d.tracks);
```

#### 4.2 `server/src/media/media-node.ts`

`publish` 接收 `tracks`，按 `mid` 建立 `source` 映射：

```diff
-  async publish(roomId: string, uid: string, sdp: string) {
+  async publish(
+    roomId: string,
+    uid: string,
+    sdp: string,
+    tracks?: { mid: string; kind: 'audio' | 'video'; source?: 'mic' | 'camera' | 'screen' }[],
+  ) {
     const room = this.rooms.get(roomId);
     const peer = room?.peers.get(uid);
     if (!room || !peer) throw new TalkError(409, 'not in room');
-    const result = await peer.publish(sdp);
+    const sourceByMid: Record<string, 'mic' | 'camera' | 'screen'> = {};
+    for (const t of tracks ?? []) {
+      if (t.source) sourceByMid[t.mid] = t.source;
+    }
+    const result = await peer.publish(sdp, sourceByMid);
```

#### 4.3 `server/src/media/room-media.ts`

新增 `TrackSource` 类型，并让 source 贯穿 producer 与 consumer：

```diff
+export type TrackSource = 'mic' | 'camera' | 'screen';
+
 export interface RecvConsumerInfo {
   ...
-  source: 'mic';
+  source: TrackSource;
 }
 
 interface Slot {
   ...
+  source?: TrackSource;
 }
```

```diff
   async publish(
     sdp: string,
+    sourceByMid: Record<string, TrackSource> = {},
   ): Promise<{ ... }> {
```

```diff
-        appData: { uid: this.uid, mid: track.mid, source: 'mic' },
+        appData: { uid: this.uid, mid: track.mid, source: sourceByMid[track.mid] ?? 'mic' },
```

```diff
     const slot = this.slots.find((s) => s.closed);
     const mid = slot?.mid ?? String(this.nextMid++);
+    const source = (producer.appData as { source?: TrackSource }).source ?? 'mic';
     const consumer = await this.recv.consume({ ... });
@@
       slot.uid = fromUid;
+      slot.source = source;
     } else {
-      this.slots.push({ mid, closed: false, consumer, producerId: producer.id, uid: fromUid });
+      this.slots.push({ mid, closed: false, consumer, producerId: producer.id, uid: fromUid, source });
     }
```

```diff
       .map((s) => ({
         ...
         kind: 'audio' as const,
-        source: 'mic' as const,
+        source: (s.source ?? 'mic') as TrackSource,
       }));
```

#### 4.4 数据流

```text
客户端 sfu.publish
  tracks[] = [{ mid, kind: 'audio', source: 'mic' | 'screen' }]
        │
        ▼
Signaling.publish(roomId, uid, sdp, tracks)
        │
        ▼
MediaNode.publish(...) → sourceByMid[mid] = source
        │
        ▼
PeerMedia.publish(sdp, sourceByMid)
  producer.appData.source = sourceByMid[mid] ?? 'mic'
        │
        ▼
PeerMedia.openConsumer(producer)
  slot.source = producer.appData.source ?? 'mic'
        │
        ▼
RecvOffer.consumers[].source = slot.source ?? 'mic'
```

### 5. 兼容性与边界

- 缺省值仍是 `mic`：旧客户端或不带 `source` 的发布行为不变；
- 只改 producer/consumer 元数据，不改 SDP、RTP、转发路径；
- 视频仍返回 `501`，`camera` 暂时不会出现在 source 中；
- `RecvConsumerInfo.kind` 仍固定为 `'audio'`，本次不扩展视频；
- 当前客户端不读取 `consumers[].source`，因此无用户可见行为变化。

### 6. 回退原因

- 当前迭代只要求提交客户端音频能力，服务端没有消费方，字段暂时没有实际收益；
- 避免服务端工作区长期保留未提交改动，与已提交的客户端产生状态漂移；
- 等真正做“接收端识别系统音 / 共享 / 录制路由 / 按来源诊断”时再恢复，并配套测试与协议更新。

回退操作（已完成）：

```bash
git restore -- server/src/media/media-node.ts \
              server/src/media/room-media.ts \
              server/src/signaling.ts
```

回退后：客户端仍会在 `sfu.publish` 中上报 `tracks[].source`，服务端会忽略它并把来源统一视为 `mic`；音频功能不受影响。

### 7. 重新落地清单

1. 恢复本部分第 4 节的 3 个文件改动；
2. 服务端补测试：带 `source` 发布后，`RecvOffer.consumers[].source` 与发布值一致；不带时回落到 `mic`；
3. 明确命名：客户端当前用 `screen` 表示“系统音”，若后续要做屏幕共享，需要区分 `system` 与 `screen`，或约定 `screen` 同时表示系统音频；
4. 更新 `talk-protocol.md` 中 `source` 的枚举说明与示例；
5. 客户端在接收侧消费该字段（例如成员列表/诊断显示“系统音”），否则该元数据仍无用户价值；
6. 回归：麦克风、系统音各发布一次，确认远端收听、静音、切换均正常。

### 8. 参考

- 客户端提交：`97e91a4`
- 客户端 source 生成：`client-win/core/media/webrtc/webrtc_media.cpp` → `TrackDescriptions`
- 协议定义：`plan/talk-protocol.md`（`tracks[].source`、`consumers[].source`）

### 9. 2026-10-22 更新：main 已合入服务端视频能力

已从 `origin/main` 合入 `62df51b`（单层 H.264，支持发布/订阅/静音摄像头），merge commit 为 `7960a9e`。

新的服务端代码已经包含本文的大部分设计：

- `PeerMedia.publish(sdp, tracks?)` 接收 `tracks`；
- `trackSource(producer)` 与 `Slot.source`；
- `consumers[].source` 按 producer 的 `appData.source` 回传；
- 视频支持 `camera` / `screen` 来源与 `media.setMute` 的 camera 分支。

但**音频来源仍固定为 `mic`**（`server/src/media/room-media.ts` 第 134 行）：

```ts
const source = track.kind === 'video' ? (declared === 'screen' ? 'screen' : 'camera') : 'mic';
```

因此：

1. 本部分第 4 节的旧补丁**不要再原样应用**，会与新的 `publish` 签名冲突；
2. 若要让系统音在服务端标记为 `screen`，只需在新实现上把音频分支改为：

   ```ts
   const source = track.kind === 'video'
     ? (declared === 'screen' ? 'screen' : 'camera')
     : (declared === 'screen' ? 'screen' : 'mic');
   ```

3. 补测试：音频发布 `source: 'screen'` 后，`RecvOffer.consumers[].source` 应为 `screen`；不带 source 时回落 `mic`；
4. 该改动仍属元数据，不影响音频转发；客户端目前仍不消费 `consumers[].source`。

## 第二部分 · 双人 SFU 对讲现场问题（2026-10-22）

> 现象来源：两台 Windows 客户端现场日志。问题一「我方听不到对方声音」；问题二「对方共享屏幕按钮是灰色的」。
> 结论：两个都是**纯客户端**缺陷，已修复并实测验证；服务端本轮无需改动（但见本部分第 5 小节的遗留状态提醒）。
> 客户端产物：`client-win/dist/rtc-talk_Setup_0.1.0.0.exe`、`client-win/dist/rtc-talk-win-20261022-2045.zip`。

### 1. 问题一：P2P 切回 SFU 后上行音频永久断流

日志表现（发布端 A）：

```text
录音初始化=true 采集中=false 采集累计帧=1609（不再增长）
音频发送 累计包=0 字节=0
```

接收端因此 `音频接收 累计包=8`、电平≈0，听不到声音。

根因链（`client-win/core/media/webrtc/webrtc_media.cpp`）：

1. `Offer()` 建 send PC 后，两个上行 sender 都是**显式关着**的（`SetSenderActive(audio/video, false)`），等 `SetMuted` / `video.start` 再打开；
2. 入房时的 `SetMuted(true)` 只作用于当时的 send PC；
3. 两人房间服务端下发 P2P，此后用户点「开麦」作用在 **P2P** 的 PC 上；
4. 第三人入房 → 服务端下发 `room.mediaMode: sfu` → `SwitchToSfu()` 里的 `SfuPause(false)` 在新 send PC 创建**之前**执行，`send_` 为空直接空转，新建的 send PC 仍是 inactive；`Accept()`（应答落地）也不恢复；
5. `RtpSender` 的 `encodings[].active=false` 会让 WebRTC `AudioSendChannel::SetSend(false)`，AudioState 空闲后调用 `ADM::StopRecording()` → 采集归零 → 上行 0 包。

之后用户在界面上再点一次「开麦」其实能救回来，但 UI 认为「已开麦」不会重发命令，于是永久单向无声。

修复：

| 位置 | 改动 |
| --- | --- |
| `WebRtcMedia::ApplySendState()` | 新增：按 `muted_` / `video_sharing_` / `sfu_paused_` 重新激活 send PC 的两个上行 sender |
| `WebRtcMedia::Accept()` | 应答落地后调用 `ApplySendState()`（覆盖 P2P→SFU 切换路径） |
| `WebRtcMedia::SfuPause()` | 复用 `ApplySendState()`，消除两处重复的激活逻辑 |
| `MediaController::Publish()` | 发布完成时若当前未静音，补发一次 `SetMuted(false)`（覆盖「发布前就开麦」的时序：那时 `send_` 还不存在，命令会落空） |
| `PeerConnection` | 记录各 kind sender 的激活状态，`GetStats()` 输出 `send_active`，日志直接看到 `音频sender=true/false` |

实测（三实例复现现场时序：开麦→切 SFU→共享→第三人入房）：

```text
发送端  音频发送 累计包=152 → 2683 → 3814   音频sender=true
接收端  音频接收 累计包=145 → 1517          bytesReceived=43483
第三人  音频接收 累计包=394 → 967
```

> 附带说明：`talk-cli` 一直在 `published` 事件里自动补发开麦，所以这个洞在 CLI 联调中长期被掩盖，只有 Qt 界面会暴露。

### 2. 问题二：共享屏幕按钮灰色

`client-win/ui/qt/window.cpp` 的 `p2p_mode_` **只在 `joined` 事件里赋值一次**，而服务端第三人入房时通过 `room.mediaMode` 把会话从 P2P 切回 SFU，客户端不发任何事件通知 UI → 按钮一直停在初始的禁用态（即使此时已经处于 SFU、完全可以共享）。反之，被切到 P2P 时按钮也不会置灰。

修复：

- `CallSession` 在应用媒体模式时发出新事件 **`mode.changed`**（`{mode: p2p|sfu}`），共三条路径：媒体未启用时按请求推断、P2P、SFU；mediasoup 初始化失败走 `media.error`，不发本事件；
- `TalkWindow::ApplyMediaMode(mode)` 抽出，`joined` 与 `mode.changed` 共用；按钮使能与 tooltip 收敛到 `ApplyVideoButtonState()`，模式变更、`video.state`、`video.start/stop` 失败三个调用点条件一致；
- 切换时在日志区明确提示：P2P →「媒体链路已切换为 P2P 直连，屏幕共享不可用」；SFU →「媒体链路已切换为 SFU，可以共享屏幕」（2026-10-24 P2P 支持视频后，文案改为「视频走点对点直传 / 视频经服务器转发」，见第 11 节）。

不是缺陷的部分：**当时一期 P2P 模式本身不支持屏幕共享**（桌面采集只挂在 `sfu.publish` 的视频上行上）。两人房间把「模式」选成 `auto` 就会被切到 P2P，此时测不到视频 —— 两台机器都改成 `sfu` 再重新加入房间即可。

> 2026-10-24 更新：P2P 已支持摄像头与屏幕共享（预发布两路视频 m-line），上述限制作废，详见第 11 节。

### 3. 现场操作注意（非代码缺陷）

1. **入房默认静音**，必须点「开麦」对方才听得到。现场日志中我方全程 `muted=true`、没有 `setMute(false)` 记录，对端成员也显示「静音」。
2. **采集源被持久化成「系统音」**：现场两台都是 `采集口=system-audio`（WASAPI loopback），对讲时录不到人声。测对讲请选回「麦克风」。

### 4. 本轮新增的排障能力

- 每 5 秒一行音频上行诊断（`MediaController::LogDeviceStats`，周期常量 `kStatsLogInterval`；界面 `media.stats` 另按 10 秒轮询）。P2P 期间没有 SFU send PC，sender 字段打印为 `n/a`：

  ```text
  音频上行 采集源=mic 静音=false SFU暂停=false 音频sender=true 视频sender=true 录音初始化=true
         loopback运行=false loopback帧=0 loopback静音包=0 loopback错误=0
  音频发送 pc=send 累计包=2791 字节=153448 RTT=24ms
  ```

- 开麦后若连续 3 个统计周期（约 15 秒）无采集帧 / 无上行包 / sender 未激活，打一条汇总 WARN（仅 SFU 判定；P2P 无 send PC 不参与）：

  ```text
  已开麦但音频上行异常：采集中=true 周期采集帧=0 音频sender=false 累计发包=0 mode=sfu
  ```

- `talk-cli` 新增 `--no-auto-unmute`（不复现自动开麦，用于复现「sender 未激活」类问题）与 `--unmute-at 秒`（N 秒后再开麦，用于复现现场操作时序）。

### 5. 服务端 toffset 遗留状态

`server/src/media/codecs.ts` 的 RTP header 扩展**白名单里不声明 `toffset`**（只保留 `abs-send-time`、`transport-wide-cc`、`video-orientation`）。mediasoup 3.13+ 的 `RtpHeaderExtensionUri` 枚举已移除 `toffset` 这个 URI，把它传给 `transport.produce()` 会抛 `invalid RtpHeaderExtensionUri`，导致 `sfu.publish` 整体失败、音视频全断；libwebrtc 会主动带 `toffset`，由服务端白名单过滤掉即可。

当前该修复**只存在于两处脆弱的地方**：本地未提交的工作区改动 + 测试机 `dist/media/codecs.js` 上的手工热补丁（备份 `codecs.js.bak-toffset`），尚未提交 git、也未走 `deploy/package.sh` + `deploy.sh` 正式发包。

| 场景 | 影响 |
| --- | --- |
| 继续用当前测试服对测 | 正常 |
| 测试服 `systemctl restart`（同一份 dist） | 正常，补丁文件仍在磁盘 |
| 用旧正式包重新部署 / 换正式环境服 | **复发**：SFU 发布整体失败 |

固化动作：提交 `codecs.ts` + 走一次服务端正式发包。

具体改动（`git diff server/src/media/codecs.ts`）：

```diff
   'http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time',
   'http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01',
   'urn:3gpp:video-orientation',
-  'urn:ietf:params:rtp-hdrext:toffset',
+  // 不列 toffset：mediasoup 3.13+ 的 RtpHeaderExtensionUri 枚举已移除该 URI，
+  // 传给它会让 transport.produce() 抛 `invalid RtpHeaderExtensionUri`，
+  // 整个 sfu.publish 失败（音视频全断）。libwebrtc 会主动带 toffset，
+  // 由这里白名单过滤掉即可。
 ]);
```

故障特征（复现判据）：客户端 `sfu.publish` 返回失败、服务端日志出现 `invalid RtpHeaderExtensionUri`，同一房间内音视频全部无流；与「只有某一路上行不通」的本地 sender 问题区分开。

固化步骤：

```bash
git add server/src/media/codecs.ts
git commit -m "fix(server): 过滤 mediasoup 不支持的 toffset 扩展"
cd deploy && ./package.sh && ./deploy.sh   # 按现场发布流程执行
```

### 6. 排查过程中踩到的两个代码坑

1. **nlohmann `value(key, default)` 按 default 类型强转**：`lb.value("last_error", std::string())` 读取 WASAPI loopback 的 HRESULT 数值（`std::atomic<long>` 写入时为 JSON number）会抛 `type_error.302`；该异常发生在统计日志的回调里（媒体 io 线程），没人捕获 → `std::terminate`，进程静默消失。改成 `contains()` + `is_number()` 判断取值，并把整段统计日志包在 try/catch 内 —— **诊断代码不能影响媒体生命周期**。
2. **`TALK_LOG_*` 传入 `fmt::format` 预拼的 `std::string` 变量时该参数打印为空**（同一行其它由相同方式构造的 `std::string` 参数正常，且已排除打包/旧 DLL 因素）。规避：把各字段作为独立参数交给日志宏格式化，不要预拼字符串。

### 7. 审查后的修正（2026-10-23）

第一轮修复经代码审查后又收了四处：

1. **P2P 房间误报「已开麦但音频上行异常」**：P2P 期间 SFU send PC 尚未发布，`send` 段即使存在也恒为 0 包，`sent == 0` 恒成立 → 开麦 15 秒必打假 WARN。现在整段上行判定限定在 `mode_ == kSfu`，P2P 下不再打印「音频发送」行；`音频sender/视频sender` 打印 `n/a`，`视频上行` 的 `pc状态` 打印 `p2p（无 SFU 连接）`。
2. **P2P 下正在共享时停不掉**：原来切 P2P 无条件禁用按钮，但共享仍在跑、文案仍是「停止共享」。按钮使能现收敛到 `TalkWindow::ApplyVideoButtonState()`（模式变更 / `video.state` / `video.start|stop` 失败 三处共用）：离房或 `backend=none` 不可点；P2P 下只有正在共享时保留「停止共享」入口，tooltip 同步区分。（2026-10-24 P2P 支持视频后，按钮不再按模式置灰，见第 11 节。）
3. **`SetSenderActive` 失败时诊断显示过期状态**：`PeerConnection` 在 `SetParameters()` 失败时把 `sender_active_[kind]` 置 `false`，避免 `send_active` 仍显示旧值 `true`、正好复现「sender 已开但没包」的误判。
4. `ApplySendState()` 的注释改为准确表述：P2P 的 PC 当时不挂视频轨，`sfu_paused_` 仅由 `p2p.state connected` 驱动，进 P2P 时关掉的是 SFU 上行 sender。（2026-10-24 起 P2P 也挂两路视频轨，切换期靠 `SetVideoReceivePaused` 去重，见第 11 节。）

### 8. 多人视频房间：服务端缺口与建议（2026-10-23）

服务端已经是多人 SFU 架构，核查结论：

- 每个房间一个 mediasoup Router，每个成员一对 `send`/`recv` transport；新人入房时由 `RoomMedia.addPeer` 把其他成员的每个 producer 逐个 `subscribe`，每人订阅 N-1 路（音视频各自独立 consumer）；
- 人数上限 `TALK_MAX_PEERS` 默认 **20**（房间 `max_peers` 可设 2~20，超限 `429 room is full`）；
- `>2` 人自动走 SFU，且离开 P2P 后 `stickySfu` 粘住不再切回（`mode-policy.ts`），多人场景不会误入 P2P。

发现两个缺口：

1. **`policy.video.enabled` / `policy.video.max_publishers`（默认 9）只定义、无执行点**：全库没有任何地方读取这两个字段，`signaling.publish` 也不校验 track 数量 → 20 人全员开视频服务端也照转，不会被拦。
2. **无 simulcast / 无按需订阅**：单层 H.264，每人下行 = 其他所有人视频路数之和，带宽随人数线性增长；服务端只有 producer 级 `pause`，没有分层码流与按订阅方暂停。

建议（按优先级）：

1. **补视频发布策略校验**：`sfu.publish` 时统计房间内未关闭的 video producer 数，超出 `video.max_publishers` 返回 `409`（语义与现有 `TalkError` 一致）；`video.enabled=false` 时拒绝 video track，只允许音频。补单测：第 10 个视频发布被拒、关闭后重新可发。
2. **客户端多路渲染**（已在 Windows 客户端落地）：接收侧按 `mid` 维护多路 sink，UI 按人数自动宫格；不再拘束于「只显示第一路」。
3. **后续可选**：引入分层/选择性订阅（H.264 当前单层，需换 codec 或增加 SVC）；按需订阅（大房间只订阅活跃发言人、其余暂停 consumer）以控制下行带宽。

测试建议：先用 3 个客户端验证「2 人各 1 路 + 1 人 2 路（摄像头/屏幕）」的 mid 分配与画面数量，再扩到 5~6 人；同时观察服务端 `consume failed` 与客户端 `recv negotiate failed` 日志。

### 9. 多人视频客户端实现（2026-10-23）

目标：demo 支持 4 人视频对讲，同时为更多人预留；一人多路（摄像头 + 屏幕）天然支持。

改动：

1. `PeerConnection`：远端视频从「只挂第一路」改为 `std::map<mid, RemoteVideo{track, sink}>`，每路一个 `RemoteVideoSink`，回调统一带各自 mid；同一 mid 重新协商时先摘旧 sink，`Close()` 全量摘除。
2. `TalkClient`：首帧日志从「进程内一次」改为「每个 mid 一次」，多路时可直接从日志确认哪几路真的解码出了画面。
3. `MediaController`：应用 SFU offer 后，为每个 video consumer 发 `video.track` 事件（mid/uid/source），供界面做角标。
4. UI：画面区改为 `QStackedLayout`（占位提示 / 宫格）；按 mid 动态建 `VideoView`，1 路 1 列、2~4 路 2x2、5~9 路 3x3；角标显示 `uid · 摄像头/屏幕`；1 秒定时器回收 5 秒无帧的画面（服务端关 producer 不会触发轨道移除回调，只能靠无帧超时）。

验证（测试服 `47.117.159.193`，4 实例 `--media-mode sfu --video` 各自共享屏幕）：

- 每个实例 `consumers=6`（3 音 + 3 视频）、`videoMids=3`（mid 1/3/5 各出首帧）、无错误；
- 3 实例时 `consumers=4`、`videoMids=2`。

限制：

- 4 人时每个客户端显示 3 路远端画面（不含本地回显，本地预览后续再做）；
- 无 simulcast，带宽随人数线性增长，超过 9 路会压成 4 列宫格；
- 服务端仍未校验 `policy.video.max_publishers`（见第 8 节）。

### 10. 一人两路（摄像头 + 屏幕）：服务端需要做的优化（2026-10-23）

结论：服务端的**转发链路本身已经支持一人多路**（`sfu.publish` 按 `tracks[]` 的 mid 建多个 producer、`subscribe`/`openConsumer` 按 producer 维度工作、`RecvConsumerInfo` 已带 mid/uid/kind/source），但**控制面还是「一人一路视频」的语义**，直接接两路会出现「停一路连带停另一路」「状态表达不了两路」「超额视频不拦」三类问题。

#### 已经可以直接复用的部分

- `media-node.ts:150` + `room-media.ts:133-142`：`tracks[]` 支持多 mid，`source` 区分 `camera` / `screen`，音频强制 `mic`；
- `room-media.ts:247-285`：consumer 按 producer 建，`slots` 按 kind 复用、mid 递增，一人两路会占两个 video slot；
- `room-media.ts:379-384`、`media-node.ts:156-158`：新人入房 / 他人新发布时全量订阅所有 producer，不需要为第二路写新逻辑；
- `negotiateOnce`（316-344）已把多路 consumers 合并进一次 `sfu.offer`，并带 `mid/uid/source` 给客户端做宫格角标。

#### P0 必须改

1. **`media.setMute` 支持 `screen`，并改成按 source 精确匹配**
   - `signaling.ts:55` 的 `SetMuteSchema` 只有 `['mic','camera']`；`signaling.ts:250-261` 只分 camera / mic 两支。
   - `room-media.ts:185-194` 的 `setMuted('camera')` 过滤条件是 `p.kind === 'video' && p.appData.source !== 'screen'` → **screen producer 永远不受任何 setMute 控制**。现在客户端把共享谎报成 `camera` 才停得下来（`webrtc_media.cpp:449-453` 有注释）。
   - 改法：schema 加 `'screen'`；`setMute` 分三支；`PeerMedia.setMuted` 过滤改为 `p.appData.source === source`（mic 仍按 kind==audio）；`MediaNode.setMuted` 的 `source` 类型同步。
2. **成员状态从 `video: { on }` 改为按 source 表达**
   - `room-manager.ts:13` 只有 `video: { on: boolean }`，`setVideo()`（129-138）只有一个开关；`room.peerUpdated` 广播的也是这个单布尔。
   - 一人两路需要 `video: { camera: boolean; screen: boolean }`，`room.join` 快照里的 peers 同步；建议保留 `on = camera || screen` 一个版本兼容旧客户端 / bot（Windows 客户端成员列表目前只读 `audio`，风险低）。
   - 若暂不想动协议，最小方案是媒体层单独维护，但成员列表就看不到「谁在共享」，不建议。
3. **`policy.video` 真正生效，且按 producer 计数**
   - `policy.ts:11-16` 的 `video.enabled`（false）/ `video.max_publishers`（9）全库无读取点（见第 8 节）。
   - 一人两路后视频 producer 数 = 人数 × 2，`max_publishers` 必须按 **producer 数**统计（不是人数），超限 `409`；`enabled=false` 时拒绝 video producer。
   - 建议同时加「同一 uid 每种 source 最多 1 路」，防止两个 camera producer 抢同一语义（现在 `room-media.ts:133-134` 只按声明分流，不校验重复）。

#### P1 重要（不做会闪断）

4. **`publish` 对同 mid 先 close 再 produce，会把「没变的那几路」一起重建**
   - `room-media.ts:135-136`：同 mid 找到旧 producer 就 `close()` 再 `produce()`；而 libwebrtc 的 offer 每次都带全部 m-line，所以任何一次重新 publish 都会重建音频 + 另一路视频的 producer。
   - 订阅端 `consumer.on('producerclose')`（259-266）会关 slot、重新 consume → 画面/声音闪断。
   - 优化：同 mid 且 `rtpParameters` 未变化时复用已有 producer（只同步 `paused`/appData），仅在 mid 消失或参数变化时 close+produce。
   - 客户端配合：一人两路必须在 join 时一次性发布两路 video m-line，之后只用 `media.setMute` / `SetSenderActive`，不要为启停单路重新 publish。
5. **producer 暂停状态没有广播**
   - `watchProducer`（220-224）只处理 `close`，`pause/resume` 不通知别人，订阅端只能靠无帧推断。
   - **2026-10-25 已解决（服务端 `333f795` + 客户端本轮适配）**：服务端新增 `media.producer{uid,mid,kind,source,paused}` 广播，客户端 UI 收到 `paused=true` 立即收掉对应画面；5 秒无帧超时降级为兜底。详见第 12 节。
   - 一人两路时 UI 需要区分「摄像头关」和「屏幕共享停」：建议新增 `media.producer` 事件（uid/mid/source/paused），或随 `room.peerUpdated` 带 `video.sources`。

#### P2 建议（人数上来再做）

6. **订阅模型是 N×(N-1)×路数**：一人两路后每端 `3(N-1)` 个 consumer，10 人房 = 27 路/端；`negotiateOnce` 每次都重建全量 SDP（316-344），报文会到几十 KB。建议：默认只订阅 camera、screen 由用户点开；或不看时 `consumer.pause()`（pause 后 mediasoup 不转发，带宽为 0，且不改变 SDP）。
7. **按需订阅 / 分层**：与第 8 节结论一致，H.264 单层，没有 simulcast，大房间只能靠「少订阅」控制下行。

#### 客户端配套（避免误以为只改服务端）

- `peer_connection.h:109-112` 的 `senders_` / `sender_active_` 按 `TrackKind` 索引，要改成按 mid，否则第二路 video sender 会覆盖第一路；
- `webrtc_media.cpp:441-455` 的 `TrackDescriptions` 要返回 2 路 video（`camera` + `screen`），`Offer()` 要加第二个 video transceiver；
- 屏幕共享的 `source` 必须从现在的 `"camera"` 改为 `"screen"`（否则服务端 P0-1 做完后会把共享和摄像头一起停）；
- `MediaController` 的 `video.start/stop` 带 source，UI 拆「摄像头」「共享屏幕」两个按钮；接收侧宫格不需要再改。

#### 验证清单

- 单人两路：另一客户端 `consumers` 含同一 uid 的 `camera` + `screen` 两个 video（source 正确、mid 不同）；
- 停屏幕共享只影响 screen producer，摄像头画面继续；`media.setMute{source:'camera'}` 不影响 screen；
- 关闭摄像头不影响共享；两路先后启停不触发音频/另一路的 producer 重建（看服务端是否打 `producer produced` 两次以上）；
- `video.max_publishers=N` 时第 N+1 个 video producer 被 `409` 拒绝，关掉一路后可再发；
- 4 人房每人两路：每端 `consumers = 3×(1音+2视) = 9`，无 `consume failed` / `recv negotiate failed`。

### 11. P2P 支持视频（2026-10-24）

背景：此前两人房间走 P2P 时只能对讲，`video.start` 在 P2P 直接返回 `409 P2P 模式暂不支持视频`。本次补齐为「P2P 也预发布摄像头 + 屏幕两路视频」，且 **服务端零改动**（`p2p.offer/answer/candidate` 只是 SDP/ICE 透明转发，不解析媒体内容）。

#### 客户端改动

1. `WebRtcMedia::StartP2p` / `AnswerP2p` 改为预加三条 m-line：音频 + 摄像头 + 屏幕，顺序与 SFU 一致（mid `0/1/2`）；新增 `ApplyP2pVideoState()` 按索引 0/1 应用摄像头/屏幕的采集状态。
2. **应答方必须先 `SetRemote(offer)` 再 `AddTrack`**：`PeerConnection::AddTrack` 在 `pc_->remote_description() != nullptr` 时改走 `pc_->AddTrack()` 复用远端 m-line 的 transceiver；否则会多出一组本地 transceiver（`p2p_send_active` 出现 `audio2/video3/video4`），应答 m-line 变成 `recvonly` → 只收不发。
3. `video.start/stop` 同时作用在 `send_`（SFU）与 `p2p_` 两个 PC 上；`SetVideoSink` 也同时下发两端，否则 P2P 画面进不了 UI。
4. 会话层删掉 P2P 的 409 分支；摄像头启停在 P2P 下跳过 `media.setMute`（P2P 没有 producer），只靠 sender `active` 控制；`SwitchToSfu` 完成后按 `camera_sharing_`/`video_sharing_` 逐路补发 `media.setMute{camera|screen,false}` 恢复 P2P 期间一直 paused 的 producer（2026-10-25 起屏幕共享也走这条路径，见第 12 节）。
5. 切换期去重：新增 `PeerConnection::SetVideoReceivePaused()`，P2P↔SFU 重叠期把已让出链路的两路视频接收 `set_enabled(false)`，避免同名 mid 的画面互盖。
6. `MediaController` 对 `pc=p2p` 的 `video.track` 按到达顺序标注 `source=camera|screen`、`uid=对端`，UI 宫格与 SFU 一致。
7. UI `ApplyVideoButtonState()` 不再按模式置灰，P2P 与 SFU 下摄像头/屏幕按钮行为一致（`p2p_mode_` 成员已移除）。

#### 验证（测试服 `47.117.159.193`，`talk-cli --media-mode auto --video --video-source screen`）

- 双端 P2P 屏幕：A 发/收 183/188 帧、B 发/收 188/180 帧，720p15，`p2p_path=host`，`p2p_send_active={audio,video2}`；
- 第三人 6 秒后加入：B `切换到 SFU` → `SFU 模式切换完成` 约 0.2s，切后 `pc=send`/`pc=recv` 帧数持续增长，C 收 114→346、发 51→189，无 409/失败/ERROR；
- P2P 下 `video.stop` → 对端收帧停在 54，`video.start` 后恢复至 121，全程无错误；
- **摄像头路径（A 摄像头 / B 屏幕，room `p2pcam2`）**：`video.start{source:camera}` 返回 `device=Logitech Webcam C925e`、`send_state=connected`；A `P2P 视频状态 camera_mid=1(有) 直播中=true ok=true`，`视频传输 pc=p2p` 发 55→115→119 帧、B 收 55→115→119 帧，720p15；`video.stop` 后 A `视频已停止 source=camera ok=true`、B 收帧停住，`video.start` 后恢复，无错误；
- `max_publishers` 默认 9，3 人 × 2 路 = 6，不会触发 409。

#### 遗留

- ~~未做能力位协商（`caps.p2p_video`）：只与最新版服务端/客户端互通，旧客户端不兼容。~~ → 2026-10-25 已补：hello 上报 `caps.p2p_video=true`，服务端双方都声明才在 `room.mediaMode.p2p_video` 透出 true（见第 12 节）。
- `talk-cli` 的 `--video-source` 默认值是 `screen`，测摄像头必须显式传 `--video-source camera`，否则会误以为摄像头未生效。

### 12. 对接服务端视频 P1（`333f795`，2026-10-25）

背景：服务端 `333f795` 上线视频 P1，客户端需要配合三件事才能完整受益（否则只是“不坏”）：停共享真正 pause producer（释放 `max_publishers` 名额）、对端停共享立即收画面（不再等 5 秒）、以及 P2P 视频能力位。

#### 客户端改动

1. **hello 上报 `caps.p2p_video`**（`call_session.cpp` `HandleOpen`）：服务端要求双方都声明才在 `room.mediaMode` 里回 `p2p_video=true`；客户端两路视频 m-line 是 P2P 预发布，能力上确实支持。
2. **屏幕共享精确暂停**（`HandleVideoStart/Stop`）：原来只有摄像头走 `media.setMute`，屏幕路注释写着“producer 常驻 resume”。改为两路统一按 `source` 精确控制：P2P 下仍直接返回（无 producer），SFU 下 `start→setMute{source,muted:false}`、`stop→setMute{source,muted:true}`；失败只告警（画面停在最后一帧），不回滚。停共享后服务端把该 producer 计为暂停，`max_publishers` 名额释放（`room-media.ts` 按“未关闭且未暂停”计数）。
3. **处理 `media.producer`**（`ui/qt/window.cpp` `HandleProducerState`）：`{uid,mid,kind,source,paused}` 里的 `mid` 是**发布方**的 mid，订阅端拿到的是 consumer mid，两者不同，所以只能按 `uid+source` 匹配本地 `video_tracks_`；`paused=true` 时立即 `RemoveVideoView`（日志“移除远端视频 mid=X：对端停止屏幕共享”），`paused=false` 不主动建视图，等第一帧到达由 `EnsureVideoView` 自然重建。`kind=audio` 直接忽略。
4. `PruneVideoViews` 的 5 秒无帧超时保留作兜底（`media.producer` 在断线重连窗口可能丢），但不再是唯一手段；抽了 `RemoveVideoView(mid, reason)` 复用移除+重排逻辑。
5. `talk-cli` 打印 `media.producer`（uid/kind/source/paused），方便联调时确认服务端是否真的 pause。
6. `HandleMediaModeRequest` 日志带上 `p2p_video`，便于核对协商结果。

#### 验证（测试服 `47.117.159.193`）

- **P2P 双端（room `p2pvid9`）**：`切换媒体模式 mode=p2p p2p_video=true`（能力位生效）；`video.stop`/`start` 全程无 `media.setMute` 请求，`video.stop` 返回 `ok=true data={}`；`P2P 视频状态 camera_mid=1(有) screen_mid=2(有) 直播中=false/true ok=true/true`。
- **3 人 SFU（room `sfuscreen2`）**：A 第 12 秒停共享、第 18 秒恢复；A 侧 `send_active.video2` 由 true→false→true、`video_capture.sharing` 同步；B/C 各收到一次 `media.producer{source:"screen",uid:"sa",paused:true}` 与 `paused:false`（后者无 `producer_id`，即 `setMute` 路径的广播），证明屏幕独立暂停/恢复生效，且音频与摄像头 producer 未被牵连。
- 客户端 `cmake --build --preset windows-x64-release` 通过（仅 websocketpp 既有 C4267 告警）。

#### 遗留

- P2P 下停共享仍只能靠对端 5 秒无帧超时收画面（P2P 无 producer、无 `media.producer`），若以后要即时，需要走 DataChannel 或服务端在 P2P 房间转发一条轻量通知。
- Qt 界面侧的“画面立即消失/恢复重建”需要真人验收（本轮只验证到事件到达与 CLI 日志）。
