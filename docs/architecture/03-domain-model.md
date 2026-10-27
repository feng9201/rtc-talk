# 03 领域模型、命令、事件与错误

## 1. 职责

领域层定义业务状态、不变量、命令和事件。它不访问网络、文件、Qt、Windows 或 WebRTC。

领域对象必须满足：

- 可被纯函数 reducer 更新。
- 可被 Fake 环境完整驱动。
- 不包含 UI 文案和线程对象。
- 不直接暴露协议 JSON。

## 2. 强类型标识

```cpp
template <class Tag>
class Id {
 public:
  explicit Id(std::string value);
  const std::string& value() const noexcept;
  bool empty() const noexcept;
 private:
  std::string value_;
};

struct SessionIdTag;
struct RoomIdTag;
struct UserIdTag;
struct CommandIdTag;
struct TrackIdTag;
struct DeviceIdTag;

using SessionId = Id<SessionIdTag>;
using RoomId    = Id<RoomIdTag>;
using UserId    = Id<UserIdTag>;
using CommandId = Id<CommandIdTag>;
using TrackId   = Id<TrackIdTag>;
using DeviceId  = Id<DeviceIdTag>;
```

`TrackId` 保存远端视频 `mid`。设备 ID 必须使用稳定标识，不使用枚举序号。

## 3. 时间类型

```cpp
using TimestampMs = std::int64_t;
using TimestampUs = std::int64_t;

struct Deadline {
  TimestampMs at_ms;
};
```

领域层接受注入的 `IClock` 结果，不直接调用系统时间。

## 4. 会话与连接

```cpp
enum class ConnectionPhase {
  Disconnected,
  Connecting,
  Handshaking,
  Ready,
  Reconnecting,
  Failed,
};

enum class JoinPhase {
  NotJoined,
  Joining,
  Joined,
  Leaving,
};

enum class DisconnectReason {
  None,
  UserRequested,
  NetworkLost,
  Kicked,
  Replaced,
  AuthExpired,
  ProtocolError,
  InternalError,
};

struct Endpoint {
  std::string url;
  std::string canonical_origin;
};

struct Session {
  SessionId id;
  Endpoint endpoint;
  ConnectionPhase connection = ConnectionPhase::Disconnected;
  JoinPhase join = JoinPhase::NotJoined;
  DisconnectReason disconnect_reason = DisconnectReason::None;
  uint64_t generation = 0;
  std::optional<TimestampMs> connected_since_ms;
  std::optional<TimestampMs> resume_deadline_ms;
};
```

不变量：

- `Handshaking -> Ready` 只能发生一次。
- `Ready` 之前不能 `Join`。
- `Kicked`、`Replaced`、`AuthExpired` 禁止自动重连。
- 新 generation 不接受旧命令结果和旧帧。

## 5. 房间与成员

```cpp
enum class RoomRole {
  Host,
  Member,
};

enum class TalkMode {
  Free,
  Ptt,
  Moderated,
};

enum class MediaModePreference {
  Auto,
  Sfu,
  P2p,
};

enum class MediaModeActual {
  Unknown,
  Sfu,
  P2p,
};

struct AudioState {
  bool muted = true;
  bool speaking = false;
  double level = 0.0;
};

struct VideoState {
  bool camera_on = false;
  bool screen_on = false;
  int active_video_publishers = 0;
};

struct Peer {
  UserId uid;
  std::string display_name;
  RoomRole role = RoomRole::Member;
  AudioState audio;
  VideoState video;
};

struct Floor {
  std::optional<UserId> holder;
  std::vector<UserId> queue;
  bool self_has_floor = false;
  std::optional<TimestampMs> expires_at_ms;
  std::string revoke_reason;
};

struct RoomPolicy {
  TalkMode talk_mode = TalkMode::Free;
  int max_speakers = 0;
  int ptt_max_hold_seconds = 60;
  bool ptt_queue = true;
  MediaModePreference media_mode = MediaModePreference::Auto;
  bool p2p_allow_relay = true;
  int max_peers = 20;
  bool video_enabled = true;
  int video_max_publishers = 9;
};

struct Room {
  RoomId id;
  RoomRole self_role = RoomRole::Member;
  MediaModeActual media_mode = MediaModeActual::Unknown;
  Floor floor;
  RoomPolicy policy;
  std::vector<Peer> peers;
};
```

不变量：

- peer uid 唯一。
- floor holder 必须存在于 room 或 self。
- queue 不得包含重复 uid。
- policy 数值必须落在协议允许范围。
- host 操作只在 `self_role == Host` 时允许。

## 6. 视频流

```cpp
enum class VideoSource {
  Camera,
  Screen,
};

struct VideoStream {
  TrackId mid;
  UserId uid;
  VideoSource source = VideoSource::Camera;
  bool producer_paused = true;
  bool receiving = false;
  TimestampMs last_frame_at_ms = 0;
  int width = 0;
  int height = 0;
};
```

不变量：

- `mid` 唯一。
- `producer_paused=true` 时 `receiving` 必须为 false。
- `now - last_frame_at_ms > 5000` 时标记 stale。
- 未知 mid 帧不得进入 UI 渲染池。

## 7. 设备

```cpp
struct AudioDevice {
  DeviceId id;
  std::string name;
  bool is_default = false;
  bool available = true;
};

struct CameraDevice {
  DeviceId id;
  std::string name;
  bool available = true;
  int max_width = 0;
  int max_height = 0;
  int max_fps = 0;
};

struct ScreenSource {
  DeviceId id;
  std::string name;
  int left = 0;
  int top = 0;
  int width = 0;
  int height = 0;
  bool primary = false;
};
```

## 8. 命令

```cpp
struct ConnectCommand {
  Endpoint endpoint;
  AuthToken token;
};

struct JoinRoomCommand {
  RoomId room_id;
  std::string display_name;
  MediaIntent media;
  RoomPolicy initial_policy;
};

struct StartCameraPreviewCommand {
  std::optional<DeviceId> device;
};

struct StartCameraCommand {
  std::optional<DeviceId> device;
};

struct StartScreenCommand {
  std::optional<DeviceId> source;
};

using CommandPayload = std::variant<
  ConnectCommand,
  DisconnectCommand,
  JoinRoomCommand,
  LeaveRoomCommand,
  SetMicMutedCommand,
  SetDeafenedCommand,
  SetVolumeCommand,
  SelectDeviceCommand,
  StartCameraPreviewCommand,
  StopCameraPreviewCommand,
  StartCameraCommand,
  StopCameraCommand,
  StartScreenCommand,
  StopScreenCommand,
  RequestFloorCommand,
  ReleaseFloorCommand,
  HostGrantCommand,
  HostMuteAllCommand,
  HostKickCommand,
  HostUpdatePolicyCommand,
  RequestStatsCommand,
  ExportDiagnosticsCommand
>;

struct Command {
  CommandId id;
  CommandPayload payload;
  Deadline deadline;
  uint64_t generation;
};
```

## 9. 事件

```cpp
using DomainEvent = std::variant<
  ConnectionChanged,
  SessionReady,
  RoomJoined,
  RoomSnapshot,
  RoomLeft,
  PeerJoined,
  PeerPatched,
  PeerLeft,
  FloorChanged,
  PolicyPatched,
  VideoTrackAdded,
  VideoTrackRemoved,
  ProducerPausedChanged,
  LocalFrameReceived,
  RemoteFrameReceived,
  AudioLevelsChanged,
  ActiveSpeakerChanged,
  CommandSucceeded,
  CommandFailed,
  DeviceListChanged,
  PreviewStateChanged,
  MediaFaulted,
  Kicked,
  Replaced,
  StatsUpdated
>;
```

事件处理：

- `RoomSnapshot` 完整替换。
- `PeerPatched` 使用 optional 字段。
- `ProducerPausedChanged` 由 mid 精确定位。
- `CommandSucceeded` 不应直接伪造服务端状态。

## 10. 错误

```cpp
enum class ErrorDomain {
  Validation,
  Auth,
  Permission,
  Network,
  Protocol,
  Device,
  Media,
  Session,
  Storage,
  Internal,
};

enum class RecoveryAction {
  None,
  Retry,
  ChooseDevice,
  Reauthenticate,
  ChangeNetwork,
  RejoinRoom,
  RestartApp,
};

struct AppError {
  ErrorDomain domain;
  int code;
  RecoveryAction action;
  bool retryable;
  std::string operation;
  std::string message_key;
  std::string detail;
  std::string correlation_id;
};
```

错误码建议：

```text
1000-1099 validation
1100-1199 auth/permission
1200-1299 network/TLS
1300-1399 protocol/server
1400-1499 session/room
1500-1599 camera/screen
1600-1699 audio
1700-1799 WebRTC
1800-1899 settings/storage
1900-1999 internal
```

## 11. reducer 测试

- 重复 join 不重复 room。
- peerLeft 后迟到 peerUpdated 不复活。
- producer paused 立即清 frame。
- producer resume 不伪造 frame。
- 主动 leave 后迟到 snapshot 不重新入房。
- kicked/replaced 不自动 reconnect。
- 旧 generation 结果被拒绝。
- 乱序 audio levels 不创建未知成员。
