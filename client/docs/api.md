# API 契约 v1

状态：目标契约。具体已提供的头文件和实现见 [状态](status.md)。
现有服务器文档有历史段落，以 handlers 和互通测试确认能力，不修改远端协议来迎合客户端。

## 1. 网络端点（使用既有服务端，不新增客户端 REST 服务）

| 端点 | 认证/用途 | 上线限制 |
|---|---|---|
| `WSS /talk/v1/ws` | Bearer JWT；子协议 `talk.v1.json` | 必须验证证书、主机名和令牌权限 |
| `GET /healthz` | 运维健康检查，非客户端登录 | 是否公开由服务端部署策略决定 |
| `GET /dev/token?uid=...&rooms=...` | 现有开发工具使用 | 仅开发，不作为生产依赖，不写入 UI 登录逻辑 |
| 业务身份服务 | 提供短期 JWT 的外部 TokenProvider | 端点尚未确定，不编造 `/login`、refresh 等现有能力 |

RK 应用不要求安装 Node/npm，也不把管理/监控端点暴露到局域网。

## 2. 信令封装与约束

```json
{"v":1,"type":"req","id":"c-42","method":"room.join","data":{"room_id":"rk-client","display":"终端","media":{"audio":{"send":true,"muted":true},"video":{"send":false}}}}
```

成功：`{v:1,type:"res",id,ok:true,data:{...}}`。
失败：`{v:1,type:"res",id,ok:false,error:{code,reason?,msg?}}`。
通知：`{v:1,type:"ntf",method,data}`。id 为字符串，双向请求分别管理。
限制：单消息 256KiB，ID/方法/数组/字符串长度有界，未知字段忽略、未知方法 501。
非 JSON、错误类型、缺字段不会导致抛出到网络回调外。非法消息限频并断开异常端。
普通请求默认 10s，SDP 请求 15s；deadline 与 generation 一起取消。
详细字段引用 [协议](../../plan/talk-protocol.md)，以下只列客户端职责。

## 3. 客户端发出的请求

| method | data / 返回关键字段 | 行为 |
|---|---|---|
| session.hello | client,caps,resume? → session_id,resumed,room?,ice_servers,heartbeat_interval,resume_ttl | 首条请求；caps 按真实能力生成 |
| session.ping | `{}` | 按服务端 heartbeat_interval |
| session.refreshIce | `{}` → ice_servers | 到期前刷新 TURN，更新相关 PC |
| session.bye | `{}` | 主动断开，禁止后续自动重连 |
| room.join | room_id,display,media,policy? → room,peers,floor,media_mode,sfu_video_codecs | audio muted=true / video send=false |
| room.leave | room_id | 本地先关麦并停止画面；释放状态不能等服务端无限返回 |
| media.setMute | source=mic,muted → audio | 开麦等成功，关麦立即执行 |
| sfu.publish | sdp,tracks=[audio/mic] → sdp | 不创建 video sender |
| sfu.restartIce | pc=send/recv | send 重新 sfu.publish；recv 等 sfu.offer，禁止并发协商 |
| room.subscribeLevels | enabled | SFU 指标订阅，恢复后重订 |
| floor.request | `{}` → granted,queue_pos? | 排队不等于可开麦；后续 floor.state 可能授予 |
| floor.release | `{}` | 本地先关 gate，再通知服务端；排队者也能退出队列 |
| host.grant | uid,allow | 主持人授予/收回 moderated 发言权 |
| host.muteAll | except?:[uid] → muted:[uid] | 清理 floor；被静音端收到 media.forceMute |
| host.kick | uid | 不能踢自己；目标收到 session.kicked + close 4003 |
| host.setPolicy | policy 局部字段 | policy schema 见既有协议，服务端广播变更 |
| call.invite | callees:[uid],media:{audio,video},room_id? → call_id,room_id | 主叫成功后入房；本端 video=false |
| call.ringing | call_id | 被叫振铃回执 |
| call.accept | call_id | 接听后另外执行 room.join |
| call.reject | call_id,code | code=486/603 |
| call.cancel | call_id | 主叫取消 |
| call.hangup | call_id | 挂断，不使用不存在的 call.bye |

字段根据既有协议和 signaling handlers 核对；实现阶段还需生成逐方法契约测试。
M1 只公开已完成的方法；未实现命令显式 Unsupported，UI 不显示“可点击但无作用”的操作。

M1 hello：Opus + H264(hw=true)、`p2p=false`、`p2p_video=false`，video.max_recv=实际已验收容量。
扩展 P2P 时 `p2p_video` 表示可承载视频 m-line，不代表必须发送视频；需双方非对称收发互通测试。

## 4. 服务端主动请求和通知

| method | 客户端责任 |
|---|---|
| sfu.offer (req) | 校验 SDP + consumers(mid/uid/kind/source) + closed mids；串行 SetRemote/Answer；deadline 内 res |
| room.mediaMode (req) | M1 不声明 P2P；未知/不支持模式明确失败，不提前关掉仍可用的链路 |
| room.peerJoined/peerUpdated/peerLeft | 增量更新 Store，与 track mapping 分离；peerLeft 清理所有相关帧和 sink |
| room.policyChanged | 替换权威 policy，重新计算 gate；不能保留旧发言权 |
| media.forceMute | 本地立即关 gate，更新 UI 与请求流程 |
| media.producer | 区分 paused/closed/camera/screen，及时清画面或显示暂停状态 |
| floor.state/revoked | 更新持有者/队列；撤销立即关 gate |
| session.kicked | 取消重连、清 token 引用、停媒体，提示 replaced/kicked |
| room.closed | 关麦、停止该房间所有轨道、回 Ready |
| room.activeSpeaker/audioLevels | 展示真实音量，不据此判定某路解码或授权成功 |
| call.* | 呼入 UI、超时/取消/挂断，不能回调中同步阻塞 GUI |

不得把 `room.peerUpdated.video.on=true` 当作已收到或显示视频；后者由流状态事件报告。

退房顺序边界：最后成员 `room.leave` 时，当前服务端会先向该成员发送
`room.closed{reason:"empty"}`，再返回 leave 成功。处于 Leaving 的客户端应将该通知
视为正常关闭，不得误报 403 或取消正常退房/bye 流程；其他 room.closed 仍按房间关闭处理。

## 5. 进程内 API（版本化目标）

```c
typedef struct rtc_client rtc_client;
typedef void (*rtc_event_cb)(void* user, const char* json, size_t bytes);
typedef struct rtc_options {
  uint32_t struct_size;
  uint32_t abi_version;
  rtc_event_cb on_event;
  void* user;
  const char* config_json;
} rtc_options;
int rtc_client_create(const rtc_options*, rtc_client** out);
int rtc_client_submit(rtc_client*, const char* method, const char* json, uint64_t* operation_id);
int rtc_client_set_safety_mute(rtc_client*); /* 只关麦；不等待普通命令队列 */
void rtc_client_destroy(rtc_client*);
```

create 校验 struct_size/version，出错令 out=null。同步返回只表示接受/拒绝；
非零 operation_id 对应恰好一个 `command.result`，包括取消、超时、断网和 shutdown。
事件 JSON 字节只在回调期间有效；回调可提交命令，不可阻塞/销毁句柄，异常被边界捕获。
destroy 只能从非回调线程调用；调用方先停止并发 API 调用；返回后不再产生回调。
TokenProvider 注入与视频 lease 接口分开定义，不经普通 JSON 事件传递原始指针或 DMA-BUF fd。

## 6. 本地命令、事件和快照

命令分域：`session.connect/disconnect/updateCredential`、`room.join/leave`、
`audio.setMuted/setGain/listDevices/selectDevice`、`video.selectStream/setLayout`、
`diagnostics.snapshot`；后续 floor/host/call 只在真实实现后开放。
本地方法由 Router 显式映射到远端方法，不无校验地透传任意 JSON。

事件公共 envelope：

```json
{"schema":1,"sequence":17,"generation":3,"name":"command.result","data":{"operation_id":42,"ok":false,"code":"busy","retryable":true}}
```

事件：session.state / room.snapshot / room.peerChanged / audio.state /
video.streamState / video.formatChanged / command.result / diagnostics.health。
所有事件携带 generation 和单调 sequence，UI 丢弃旧代事件；帧不经过 JSON 事件总线。
公开快照可重建 UI；含 room、peers、policy、floor、设备、授权/静音、流和健康度，不含 JWT/ICE 密钥。

本地错误独立枚举：InvalidArgument、InvalidState、Busy、Unsupported、Unauthorized、
Forbidden、TransportFailed、Timeout、DeviceUnavailable、CodecUnsupported、
DecodeFailed、RenderUnavailable、Cancelled、Internal；可附 remote_code，不把业务授权失败显示成网络故障。

## 7. 视频内部 ports

`Decoder::Submit(EncodedAccessUnit)` 非阻塞/有界，返回 Accepted/Backpressure/NeedKeyframe/Unsupported。
输出 `DecodedFrameLease` 含 immutable 描述 + 引用所有权；NativeBuffer 包装保留同一 lease。
`VideoSink::OnFrame` 由 WebRTC 调度调用，只写 latest mailbox；`Renderer::Present` 交给显示执行器。
渲染提交生成 buffer lease，pending 替换不影响 committed 的租约；`wl_buffer.release` 唯一正常归还点。
所有 track/stream key = `{generation, uid, source, mid}`，不能只用 uid 区分 camera/screen。
