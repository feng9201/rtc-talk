// File: ui/qt/window.cpp
// Brief: 实现 Qt 主窗口的控件、命令与事件处理。
#include "window.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedLayout>
#include <QStringList>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>

#include "core_client.h"
#include "video_view.h"

namespace {

// 首项固定为系统默认通信设备（空 id），其余为枚举结果。
void FillDevices(QComboBox* box, const QJsonArray& devices,
                 const QString& selected) {
  QSignalBlocker blocker(box);
  box->clear();
  box->addItem(QStringLiteral("系统默认（通信设备）"), QString());
  for (const auto& value : devices) {
    const QJsonObject device = value.toObject();
    box->addItem(device["name"].toString(), device["id"].toString());
  }
  const int index = box->findData(selected);
  box->setCurrentIndex(index < 0 ? 0 : index);
}

}  // namespace

TalkWindow::TalkWindow(QWidget* parent)
    : QWidget(parent), settings_(AppSettings::Load()) {
  BuildUi();
  ApplySettings();
  ConnectSignals();
  ResetButtons();
  QTimer::singleShot(0, this, [this] { RefreshDevices(); });
}

TalkWindow::~TalkWindow() {
  settings_.url = url_->text();
  settings_.uid = uid_->text();
  settings_.room = room_->text();
  // 命令行覆盖只对本次启动生效，不写回配置。
  if (backend_override_.isEmpty()) {
    settings_.backend = backend_->currentData().toString();
  }
  settings_.media_mode = mode_->currentData().toString();
  settings_.audio_source = source_->currentData().toString();
  settings_.recording_device = recording_->currentData().toString();
  settings_.playout_device = playout_->currentData().toString();
  settings_.Save();
}

void TalkWindow::BuildUi() {
  setWindowTitle(QStringLiteral("RTC Talk · Windows"));
  resize(1180, 760);
  setStyleSheet(
      "QPushButton,QLineEdit,QComboBox{min-height:34px;font-size:16px;}"
      "QLabel,QCheckBox{font-size:16px;}");

  auto* layout = new QVBoxLayout(this);
  auto* form = new QFormLayout;

  url_ = new QLineEdit;
  uid_ = new QLineEdit;
  room_ = new QLineEdit;
  token_ = new QLineEdit;
  token_->setEchoMode(QLineEdit::Password);
  token_->setPlaceholderText(QStringLiteral("仅保存在内存，不写配置"));
  dev_ = new QCheckBox(QStringLiteral("开发测试：从 /dev/token 获取凭证"));

  backend_ = new QComboBox;
  backend_->addItem(QStringLiteral("仅信令（不访问声卡）"), "none");
  backend_->addItem(QStringLiteral("合成音频（不访问声卡）"), "fake");
  backend_->addItem(QStringLiteral("WASAPI（Core Audio）"), "wasapi");

  mode_ = new QComboBox;
  mode_->addItem(QStringLiteral("自动（两人时 P2P）"), "auto");
  mode_->addItem(QStringLiteral("仅 SFU"), "sfu");
  mode_->addItem(QStringLiteral("强制 P2P"), "p2p");
  mode_->setToolTip(
      QStringLiteral("入房时作为策略提交；2 人且双方支持 P2P 时走直连"));

  source_ = new QComboBox;
  source_->addItem(QStringLiteral("麦克风"), "mic");
  source_->addItem(QStringLiteral("系统音（默认播放设备）"), "system");
  source_->setToolTip(
      QStringLiteral("系统音采集默认播放设备的混音；可随时切换，无需重连"));

  recording_ = new QComboBox;
  playout_ = new QComboBox;
  recording_->setMinimumWidth(200);
  playout_->setMinimumWidth(200);
  recording_->setToolTip(QStringLiteral("采集设备：默认使用系统通信设备"));
  playout_->setToolTip(QStringLiteral("播放设备：默认使用系统通信设备"));
  devices_ = new QPushButton(QStringLiteral("枚举设备"));

  form->addRow(QStringLiteral("信令服务"), url_);
  form->addRow(QStringLiteral("用户标识"), uid_);
  form->addRow(QStringLiteral("房间 ID"), room_);
  form->addRow("Token", token_);
  form->addRow(dev_);
  form->addRow(QStringLiteral("音频后端"), backend_);
  form->addRow(QStringLiteral("媒体模式"), mode_);
  form->addRow(QStringLiteral("采集源"), source_);
  auto* audio = new QHBoxLayout;
  audio->addWidget(recording_);
  audio->addWidget(playout_);
  audio->addWidget(devices_);
  form->addRow(QStringLiteral("采集 / 播放"), audio);
  volume_ = new QSlider(Qt::Horizontal);
  volume_->setRange(0, 100);
  volume_->setValue(50);
  form->addRow(QStringLiteral("播放音量"), volume_);
  layout->addLayout(form);

  connect_ = new QPushButton(QStringLiteral("连接"));
  disconnect_ = new QPushButton(QStringLiteral("断开"));
  join_ = new QPushButton(QStringLiteral("加入房间"));
  leave_ = new QPushButton(QStringLiteral("离开"));
  mute_ = new QPushButton(QStringLiteral("开麦"));
  camera_ = new QPushButton(QStringLiteral("开摄像头"));
  camera_->setToolTip(QStringLiteral("把本机摄像头共享给对端（P2P 与 SFU 均支持）"));
  video_ = new QPushButton(QStringLiteral("共享屏幕"));
  video_->setToolTip(QStringLiteral("把本机桌面共享给对端（P2P 与 SFU 均支持）"));
  auto* buttons = new QHBoxLayout;
  for (auto* button :
       {connect_, disconnect_, join_, leave_, mute_, camera_, video_}) {
    buttons->addWidget(button);
  }
  layout->addLayout(buttons);

  state_ = new QLabel(QStringLiteral("未连接"));
  media_ = new QLabel(QStringLiteral("媒体：未建立"));
  layout->addWidget(state_);
  layout->addWidget(media_);

  // 中部左右分栏：左侧远端画面自适应伸展，右侧成员与日志固定宽度。
  // 画面区用堆叠布局：没有视频时显示提示，有画面时切到宫格。
  video_area_ = new QWidget;
  video_area_->setMinimumSize(480, 270);
  video_stack_ = new QStackedLayout(video_area_);
  video_stack_->setContentsMargins(0, 0, 0, 0);
  video_placeholder_ = new QLabel(QStringLiteral("等待远端视频"));
  video_placeholder_->setAlignment(Qt::AlignCenter);
  video_placeholder_->setStyleSheet(
      QStringLiteral("background:#000;color:#9e9e9e;"));
  video_grid_widget_ = new QWidget;
  video_grid_widget_->setStyleSheet(QStringLiteral("background:#000;"));
  video_grid_ = new QGridLayout(video_grid_widget_);
  video_grid_->setContentsMargins(0, 0, 0, 0);
  video_grid_->setSpacing(4);
  video_stack_->addWidget(video_placeholder_);
  video_stack_->addWidget(video_grid_widget_);
  video_stack_->setCurrentWidget(video_placeholder_);
  members_ = new QListWidget;
  log_ = new QPlainTextEdit;
  log_->setReadOnly(true);
  log_->setMaximumBlockCount(100);
  auto* side = new QVBoxLayout;
  side->addWidget(members_, 1);
  side->addWidget(log_, 1);
  auto* side_panel = new QWidget;
  side_panel->setFixedWidth(320);
  side_panel->setLayout(side);
  auto* center = new QHBoxLayout;
  center->addWidget(video_area_, 1);
  center->addWidget(side_panel);
  layout->addLayout(center, 1);

  // 对端停止共享或离房后不会再有该 mid 的帧，靠定时回收画面。
  video_prune_timer_ = new QTimer(this);
  video_prune_timer_->setInterval(1000);
  connect(video_prune_timer_, &QTimer::timeout, this,
          [this] { PruneVideoViews(); });
  video_prune_timer_->start();

  http_ = new QNetworkAccessManager(this);
}

void TalkWindow::ApplySettings() {
  url_->setText(settings_.url);
  uid_->setText(settings_.uid);
  room_->setText(settings_.room);
  int index = backend_->findData(settings_.backend);
  backend_->setCurrentIndex(index < 0 ? 0 : index);
  // 旧版本会把冒烟用的 none 持久化，导致启动后完全没声音。
  if (settings_.backend == "none") {
    settings_.backend = "wasapi";
    index = backend_->findData(settings_.backend);
    backend_->setCurrentIndex(index < 0 ? 0 : index);
    settings_.Save();
    AppendLog(QStringLiteral(
        "历史配置为“仅信令”，已恢复为 WASAPI；无需声卡请手动选择"));
  }
  index = mode_->findData(settings_.media_mode);
  mode_->setCurrentIndex(index < 0 ? 0 : index);
  index = source_->findData(settings_.audio_source);
  source_->setCurrentIndex(index < 0 ? 0 : index);
  active_source_ = source_->currentData().toString();
  FillDevices(recording_, QJsonArray(), settings_.recording_device);
  FillDevices(playout_, QJsonArray(), settings_.playout_device);
}

void TalkWindow::ConnectSignals() {
  connect(connect_, &QPushButton::clicked, this, &TalkWindow::ConnectServer);
  connect(disconnect_, &QPushButton::clicked, this,
          [this] { SendCommand("disconnect"); });
  connect(join_, &QPushButton::clicked, this, [this] {
    join_->setEnabled(false);
    SendCommand("room.join", {{"room_id", room_->text()},
                              {"display", uid_->text()}});
  });
  connect(leave_, &QPushButton::clicked, this,
          [this] { SendCommand("room.leave"); });
  connect(mute_, &QPushButton::clicked, this, [this] {
    mute_->setEnabled(false);
    SendCommand("media.setMute", {{"muted", !muted_}});
  });
  connect(camera_, &QPushButton::clicked, this, [this] {
    // 结果由 video.state / result 事件回填，期间禁用以防连点。
    camera_->setEnabled(false);
    SendCommand(camera_on_ ? "video.stop" : "video.start",
                {{"source", "camera"}});
  });
  connect(video_, &QPushButton::clicked, this, [this] {
    // 结果由 video.state / result 事件回填，期间禁用以防连点。
    video_->setEnabled(false);
    SendCommand(video_sharing_ ? "video.stop" : "video.start",
                {{"source", "screen"}});
  });
  connect(devices_, &QPushButton::clicked, this,
          &TalkWindow::RefreshDevices);
  connect(backend_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this] {
            backend_override_.clear();
            settings_.backend = backend_->currentData().toString();
            settings_.Save();
            ResetButtons();
            RefreshDevices();
          });
  connect(mode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this] {
            settings_.media_mode = mode_->currentData().toString();
            settings_.Save();
          });
  connect(source_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this] {
            const QString source = source_->currentData().toString();
            settings_.audio_source = source;
            settings_.Save();
            UpdateSourceControls();
            const bool connected = disconnect_->isEnabled();
            if (connected && backend_->currentData().toString() != "none") {
              // 已连接（含入房）：运行时下发切换命令，失败在 HandleResult 回滚。
              SendCommand("audio.setSource", {{"source", source}});
              AppendLog(QStringLiteral("正在切换采集源：%1")
                            .arg(source == "system"
                                     ? QStringLiteral("系统音")
                                     : QStringLiteral("麦克风")));
              return;
            }
            RefreshDevices();
            if (connect_->isEnabled()) {
              active_source_ = source;
              AppendLog(QStringLiteral("采集源已更新，下次连接生效"));
            }
          });
  connect(recording_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this] {
            settings_.recording_device =
                recording_->currentData().toString();
            settings_.Save();
            if (connect_->isEnabled()) {
              AppendLog(QStringLiteral("采集设备已更新，下次连接生效"));
            }
          });
  connect(playout_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this] {
            settings_.playout_device = playout_->currentData().toString();
            settings_.Save();
            if (connect_->isEnabled()) {
              AppendLog(QStringLiteral("播放设备已更新，下次连接生效"));
            }
          });
  connect(volume_, &QSlider::sliderReleased, this, [this] {
    SendCommand("audio.volume", {{"gain", volume_->value() / 100.0}});
  });
  auto* stats = new QTimer(this);
  stats->setInterval(10000);
  connect(stats, &QTimer::timeout, this, [this] {
    if (leave_->isEnabled() &&
        backend_->currentData().toString() != "none") {
      SendCommand("media.stats");
    }
  });
  stats->start();
}

QJsonObject TalkWindow::CoreConfig() const {
  return QJsonObject{
      {"audio_backend", backend_->currentData().toString()},
      {"audio_source", source_->currentData().toString()},
      {"media_mode", mode_->currentData().toString()},
      {"recording_device", recording_->currentData().toString()},
      {"playout_device", playout_->currentData().toString()}};
}

bool TalkWindow::EnsureCore() {
  if (core_ == nullptr) {
    core_ = new CoreClient(this);
    connect(core_, &CoreClient::Event, this, &TalkWindow::HandleEvent);
    connect(core_, &CoreClient::VideoFrame, this,
            [this](const QImage& frame, const QString& mid) {
              // 多路并存：每个 mid 一个画面，角标显示 uid/采集源。
              EnsureVideoView(mid)->PushFrame(frame);
              video_last_frame_ms_[mid] = QDateTime::currentMSecsSinceEpoch();
            });
  }
  if (!core_->Start(CoreConfig())) {
    AppendLog(QStringLiteral("创建核心库失败"));
    return false;
  }
  core_->SetVideoCallback(true);
  return true;
}

void TalkWindow::SendCommand(const QString& method,
                             const QJsonObject& data) {
  if (core_ == nullptr || !core_->Submit(method, data)) {
    AppendLog(QStringLiteral("命令提交失败"));
  }
}

void TalkWindow::ConnectServer() {
  const QUrl endpoint(url_->text());
  if (endpoint.scheme() != "ws" || endpoint.host().isEmpty() ||
      endpoint.hasQuery()) {
    AppendLog(QStringLiteral("测试版本要求不含 query 的 ws:// 地址"));
    return;
  }
  if (!EnsureCore()) {
    return;
  }
  connect_->setEnabled(false);
  devices_->setEnabled(false);
  backend_->setEnabled(false);
  mode_->setEnabled(false);
  recording_->setEnabled(false);
  playout_->setEnabled(false);
  if (!dev_->isChecked()) {
    if (token_->text().isEmpty()) {
      AppendLog(QStringLiteral("请填写 Token 或显式启用开发模式"));
      ResetButtons();
      return;
    }
    SendCommand("connect",
                {{"url", url_->text()}, {"token", token_->text()}});
    return;
  }
  // 开发模式：向测试服务端换取短期 Token。
  QUrl token_endpoint = endpoint;
  token_endpoint.setScheme("http");
  token_endpoint.setPath("/dev/token");
  QUrlQuery query;
  query.addQueryItem("uid", uid_->text());
  query.addQueryItem("rooms", room_->text());
  token_endpoint.setQuery(query);
  auto* reply = http_->get(QNetworkRequest(token_endpoint));
  QTimer::singleShot(8000, reply, [reply] {
    if (reply->isRunning()) {
      reply->abort();
    }
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    const QJsonObject data =
        QJsonDocument::fromJson(reply->readAll()).object();
    if (reply->error() != QNetworkReply::NoError ||
        data["token"].toString().isEmpty()) {
      AppendLog(QStringLiteral("开发凭证获取失败"));
      ResetButtons();
    } else {
      SendCommand("connect", {{"url", url_->text()},
                              {"token", data["token"].toString()}});
    }
    reply->deleteLater();
  });
}

void TalkWindow::RefreshDevices() {
  if (!connect_->isEnabled()) {
    return;  // 连接或通话中不改动核心库
  }
  const bool audio = backend_->currentData().toString() == "wasapi";
  const bool system = source_->currentData().toString() == "system";
  source_->setEnabled(audio);
  playout_->setEnabled(audio);
  if (!audio) {
    recording_->setEnabled(false);
    FillDevices(recording_, QJsonArray(), settings_.recording_device);
    FillDevices(playout_, QJsonArray(), settings_.playout_device);
    return;
  }
  if (system) {
    UpdateSourceControls();
  } else {
    recording_->setEnabled(true);
  }
  if (!EnsureCore()) {
    return;
  }
  SendCommand("audio.devices");
}

void TalkWindow::UpdateSourceControls() {
  const bool audio = backend_->currentData().toString() == "wasapi";
  const bool system = source_->currentData().toString() == "system";
  const bool editable = connect_->isEnabled();
  // 入房后仍允许切换采集源（运行时生效），其余设备需重连生效。
  const bool live = disconnect_->isEnabled();
  source_->setEnabled(audio && (editable || live));
  if (!audio) {
    recording_->setEnabled(false);
    return;
  }
  if (system) {
    QSignalBlocker blocker(recording_);
    recording_->clear();
    recording_->addItem(QStringLiteral("系统音（默认播放设备）"), "system");
    recording_->setEnabled(false);
    return;
  }
  if (recording_->currentData().toString() == "system") {
    FillDevices(recording_, QJsonArray(), settings_.recording_device);
  }
  recording_->setEnabled(editable);
}

void TalkWindow::ResetButtons() {
  connect_->setEnabled(true);
  disconnect_->setEnabled(false);
  join_->setEnabled(false);
  leave_->setEnabled(false);
  mute_->setEnabled(false);
  camera_->setEnabled(false);
  video_->setEnabled(false);
  devices_->setEnabled(true);
  backend_->setEnabled(true);
  mode_->setEnabled(true);
  const bool audio = backend_->currentData().toString() == "wasapi";
  source_->setEnabled(audio);
  playout_->setEnabled(audio);
  UpdateSourceControls();
  muted_ = true;
  mute_->setText(QStringLiteral("开麦"));
  camera_on_ = false;
  camera_->setText(QStringLiteral("开摄像头"));
  video_sharing_ = false;
  video_->setText(QStringLiteral("共享屏幕"));
}

void TalkWindow::RefreshMembers() {
  members_->clear();
  for (auto it = peers_.begin(); it != peers_.end(); ++it) {
    const QJsonObject& peer = it.value();
    members_->addItem(QStringLiteral("%1 · %2 · %3")
                          .arg(peer["display"].toString(it.key()),
                               peer["role"].toString(),
                               peer["audio"].toObject()["muted"].toBool()
                                   ? QStringLiteral("静音")
                                   : QStringLiteral("开麦")));
  }
}

void TalkWindow::SetLogPath(const QString& path) {
  AppendLog(QStringLiteral("日志文件：%1").arg(path));
}

void TalkWindow::SetBackendOverride(const QString& backend) {
  const int index = backend_->findData(backend);
  if (index < 0) {
    return;
  }
  backend_override_ = backend;
  const QSignalBlocker blocker(backend_);
  backend_->setCurrentIndex(index);
  AppendLog(QStringLiteral("音频后端临时覆盖为 %1（不写入配置）")
                .arg(backend_->currentText()));
}

void TalkWindow::AppendLog(const QString& text) {
  log_->appendPlainText(text);
}

void TalkWindow::HandleEvent(const QString& name, const QJsonObject& data) {
  if (name == "state") {
    HandleState(data);
    return;
  }
  if (name == "ready") {
    self_uid_ = data["uid"].toString();
    state_->setText(QStringLiteral("已连接"));
    disconnect_->setEnabled(true);
    join_->setEnabled(true);
    return;
  }
  if (name == "joined") {
    HandleJoined(data);
    return;
  }
  if (name == "published") {
    mute_->setEnabled(true);
    media_->setText(data["mode"].toString() == "p2p"
                        ? QStringLiteral("媒体：P2P 已连接，默认静音")
                        : QStringLiteral("媒体：发布成功，默认静音"));
    return;
  }
  if (name == "p2p.started") {
    media_->setText(
        QStringLiteral("P2P 协商中（%1）").arg(data["role"].toString()));
    AppendLog(QStringLiteral("P2P 会话 %1 已启动，对端 %2")
                  .arg(data["session"].toString(), data["peer"].toString()));
    return;
  }
  if (name == "p2p.state") {
    const QString state = data["state"].toString();
    media_->setText(
        state == "connected"
            ? QStringLiteral("媒体：P2P 已连接（%1）")
                  .arg(data["path"].toString())
            : QStringLiteral("媒体：P2P %1").arg(state));
    AppendLog(QStringLiteral("P2P 状态：%1 %2")
                  .arg(state, data["path"].toString()));
    return;
  }
  if (name == "video.track") {
    // 服务端 consumer 元信息：用来给每路画面标出对端 uid 与采集源。
    const QString mid = data["mid"].toString();
    if (!mid.isEmpty()) {
      video_tracks_[mid] = data;
      UpdateVideoLabel(mid);
      AppendLog(QStringLiteral("远端视频轨 mid=%1 uid=%2 source=%3")
                    .arg(mid, data["uid"].toString(),
                         data["source"].toString()));
    }
    return;
  }
  if (name == "media.producer") {
    HandleProducerState(data);
    return;
  }
  if (name == "mode.changed") {
    const QString mode = data["mode"].toString(QStringLiteral("sfu"));
    ApplyMediaMode(mode);
    AppendLog(mode == QStringLiteral("p2p")
                  ? QStringLiteral("媒体链路已切换为 P2P 直连，视频走点对点直传")
                  : QStringLiteral("媒体链路已切换为 SFU，视频经服务器转发"));
    return;
  }
  if (name == "left" || name == "room.closed") {
    peers_.clear();
    RefreshMembers();
    join_->setEnabled(true);
    leave_->setEnabled(false);
    mute_->setEnabled(false);
    muted_ = true;
    mute_->setText(QStringLiteral("开麦"));
    // 离房后采集已由核心停止，本地只需清画面并注销回调。
    camera_on_ = false;
    camera_->setText(QStringLiteral("开摄像头"));
    video_sharing_ = false;
    video_->setText(QStringLiteral("共享屏幕"));
    ApplyVideoButtonState();
    ClearVideoViews();
    if (core_ != nullptr) {
      core_->SetVideoCallback(false);
    }
    return;
  }
  if (name == "room.peerJoined") {
    peers_[data["uid"].toString()] = data;
    RefreshMembers();
    return;
  }
  if (name == "room.peerLeft") {
    peers_.remove(data["uid"].toString());
    RefreshMembers();
    return;
  }
  if (name == "room.peerUpdated") {
    const QString uid = data["uid"].toString();
    if (uid == self_uid_) {
      return;
    }
    QJsonObject peer = peers_.value(uid);
    for (auto it = data.begin(); it != data.end(); ++it) {
      peer[it.key()] = it.value();
    }
    peers_[uid] = peer;
    RefreshMembers();
    return;
  }
  if (name == "media.forceMute" || name == "muted") {
    muted_ = name == "media.forceMute" || data["muted"].toBool(true);
    mute_->setText(muted_ ? QStringLiteral("开麦") : QStringLiteral("静音"));
    return;
  }
  if (name == "media.state") {
    media_->setText(QStringLiteral("媒体 %1：状态 %2")
                        .arg(data["pc"].toString())
                        .arg(data["state"].toInt()));
    return;
  }
  if (name == "video.state") {
    HandleVideoState(data);
    return;
  }
  if (name == "media.track") {
    if (data["kind"].toString() == "video") {
      AppendLog(QStringLiteral("远端视频轨道 mid=%1")
                    .arg(data["mid"].toString()));
    }
    return;
  }
  if (name == "result") {
    HandleResult(data);
    return;
  }
  if (name == "media.error" || name == "error") {
    HandleMediaError(name, data);
  }
}

void TalkWindow::HandleState(const QJsonObject& data) {
  const QString state = data["state"].toString();
  state_->setText(QStringLiteral("信令：%1").arg(state));
  if (state == "disconnected") {
    ResetButtons();
    peers_.clear();
    RefreshMembers();
    media_->setText(QStringLiteral("媒体：未建立"));
    ClearVideoViews();
    if (core_ != nullptr) {
      core_->SetVideoCallback(false);
    }
  } else if (state == "reconnecting") {
    mute_->setEnabled(false);
    join_->setEnabled(false);
    muted_ = true;
    mute_->setText(QStringLiteral("开麦"));
  }
}

void TalkWindow::HandleJoined(const QJsonObject& data) {
  peers_.clear();
  for (const auto& value : data["peers"].toArray()) {
    const QJsonObject peer = value.toObject();
    peers_[peer["uid"].toString()] = peer;
  }
  RefreshMembers();
  join_->setEnabled(false);
  leave_->setEnabled(true);
  muted_ = true;
  if (data["media_mode"].toString(QStringLiteral("sfu")) == "p2p") {
    const QJsonObject p2p = data["p2p"].toObject();
    media_->setText(QStringLiteral("P2P 协商中：%1（%2）")
                        .arg(p2p["peer"].toString(), p2p["role"].toString()));
  } else {
    media_->setText(QStringLiteral("SFU：发布中"));
  }
  if (backend_->currentData().toString() == "none") {
    mute_->setEnabled(true);
  }
  // 重新入房后恢复帧回调。
  if (core_ != nullptr) {
    core_->SetVideoCallback(true);
  }
  ApplyMediaMode(data["media_mode"].toString(QStringLiteral("sfu")));
}

// 服务端会在房间人数变化时下发 room.mediaMode（2 人 P2P / 3 人以上 SFU），
// 两种链路都支持摄像头与屏幕共享，这里只需按最新状态刷新按钮。
void TalkWindow::ApplyMediaMode(const QString& mode) {
  // 目前两种模式对视频的控制一致，保留 mode 入参便于后续按链路加差异限制。
  Q_UNUSED(mode);
  ApplyVideoButtonState();
}

// 两路视频按钮的使能条件只在这里定义：离房或后端为 none 时不可点。
// P2P 与 SFU 都支持视频，按钮不再按模式置灰。
void TalkWindow::ApplyVideoButtonState() {
  const bool backend_ok =
      backend_->currentData().toString() != QStringLiteral("none");
  const bool room_ok = leave_->isEnabled() && backend_ok;
  camera_->setEnabled(room_ok);
  video_->setEnabled(room_ok);
  camera_->setToolTip(
      QStringLiteral("把本机摄像头共享给对端（P2P 与 SFU 均支持）"));
  video_->setToolTip(
      QStringLiteral("把本机桌面共享给对端（P2P 与 SFU 均支持）"));
}

void TalkWindow::HandleVideoState(const QJsonObject& data) {
  const bool sharing = data["sharing"].toBool();
  const QString source = data["source"].toString(QStringLiteral("screen"));
  if (source == QStringLiteral("camera")) {
    const bool changed = sharing != camera_on_;
    camera_on_ = sharing;
    camera_->setText(camera_on_ ? QStringLiteral("关闭摄像头")
                                : QStringLiteral("开摄像头"));
    ApplyVideoButtonState();
    // 入房时也会收到 sharing=false，不能把发布/连接状态覆盖掉。
    if (camera_on_) {
      media_->setText(QStringLiteral("媒体：摄像头已开启"));
    } else if (media_->text().contains(QStringLiteral("摄像头"))) {
      media_->setText(QStringLiteral("媒体：摄像头已关闭"));
    }
    if (changed) {
      AppendLog(camera_on_ ? QStringLiteral("摄像头已开启")
                           : QStringLiteral("摄像头已关闭"));
    }
    return;
  }

  const bool changed = sharing != video_sharing_;
  video_sharing_ = sharing;
  video_->setText(video_sharing_ ? QStringLiteral("停止共享")
                                 : QStringLiteral("共享屏幕"));
  ApplyVideoButtonState();
  // 入房时也会收到 sharing=false，不能把发布/连接状态覆盖掉。
  if (video_sharing_) {
    media_->setText(QStringLiteral("媒体：正在共享屏幕"));
  } else if (media_->text().contains(QStringLiteral("共享"))) {
    media_->setText(QStringLiteral("媒体：共享已停止"));
  }
  if (changed) {
    AppendLog(video_sharing_ ? QStringLiteral("屏幕共享已开启")
                             : QStringLiteral("屏幕共享已停止"));
  }
}

void TalkWindow::HandleResult(const QJsonObject& data) {
  const QString method = data["method"].toString();
  const bool ok = data["ok"].toBool();
  if (!ok) {
    AppendLog(QStringLiteral("%1 失败，错误码 %2")
                  .arg(method)
                  .arg(data["error"].toObject()["code"].toInt()));
    if (method == "room.join") {
      join_->setEnabled(true);
    }
  }
  if (method == "media.setMute") {
    mute_->setText(muted_ ? QStringLiteral("开麦") : QStringLiteral("静音"));
    mute_->setEnabled(leave_->isEnabled());
  }
  if (!ok && (method == "video.start" || method == "video.stop")) {
    // 失败时恢复可点，文案与实际状态一致。
    ApplyVideoButtonState();
  }
  if (method == "audio.devices" && ok) {
    const QJsonObject list = data["data"].toObject();
    const bool system = source_->currentData().toString() == "system";
    if (system) {
      QSignalBlocker blocker(recording_);
      recording_->clear();
      recording_->addItem(QStringLiteral("系统音（默认播放设备）"),
                          "system");
      recording_->setEnabled(false);
    } else {
      FillDevices(recording_, list["recording"].toArray(),
                  settings_.recording_device);
      recording_->setEnabled(connect_->isEnabled());
    }
    FillDevices(playout_, list["playout"].toArray(),
                settings_.playout_device);
    AppendLog(QStringLiteral("音频设备：采集 %1 个，播放 %2 个")
                  .arg(list["recording"].toArray().size())
                  .arg(list["playout"].toArray().size()));
    if (!system && list["recording"].toArray().isEmpty()) {
      AppendLog(
          QStringLiteral("未检测到采集设备（麦克风），发布/开麦会失败，只能收听"));
    }
  }
  if (method == "audio.setSource") {
    if (ok) {
      active_source_ = source_->currentData().toString();
      AppendLog(QStringLiteral("采集源已切换为：%1")
                    .arg(active_source_ == "system"
                             ? QStringLiteral("系统音（默认播放设备）")
                             : QStringLiteral("麦克风")));
      if (active_source_ != "system") {
        SendCommand("audio.devices");  // 切回麦克风：重新枚举采集设备
      }
    } else {
      const QSignalBlocker blocker(source_);
      settings_.audio_source = active_source_;
      settings_.Save();
      const int index = source_->findData(active_source_);
      source_->setCurrentIndex(index < 0 ? 0 : index);
      UpdateSourceControls();
      if (active_source_ != "system") {
        SendCommand("audio.devices");  // 回滚到麦克风：重新枚举采集设备
      }
      AppendLog(QStringLiteral("采集源切换失败，已恢复为上次成功的源"));
    }
  }
  if (method == "media.stats" && ok) {
    AppendLog(QString::fromUtf8(QJsonDocument(data["data"].toObject())
                                    .toJson(QJsonDocument::Compact)));
  }
}

VideoView* TalkWindow::EnsureVideoView(const QString& mid) {
  const auto it = video_views_.find(mid);
  if (it != video_views_.end()) {
    return it.value();
  }
  auto* view = new VideoView(video_grid_widget_);
  // 宫格内单路最小 240x135，四路 2x2 时整块仍保持在 480x270 以上。
  view->setMinimumSize(240, 135);
  video_views_.insert(mid, view);
  video_last_frame_ms_.insert(mid, QDateTime::currentMSecsSinceEpoch());
  UpdateVideoLabel(mid);
  RelayoutVideoGrid();
  AppendLog(QStringLiteral("远端视频路数 %1（新增 mid=%2）")
                .arg(video_views_.size())
                .arg(mid));
  return view;
}

void TalkWindow::UpdateVideoLabel(const QString& mid) {
  const auto it = video_views_.find(mid);
  if (it == video_views_.end()) {
    return;
  }
  const QJsonObject track = video_tracks_.value(mid);
  const QString uid = track["uid"].toString();
  const QString source = track["source"].toString();
  QString label = uid.isEmpty() ? mid : uid;
  if (source == "screen") {
    label += QStringLiteral(" · 屏幕");
  } else if (source == "camera") {
    label += QStringLiteral(" · 摄像头");
  }
  it.value()->SetLabel(label);
}

void TalkWindow::RelayoutVideoGrid() {
  // 只摘下布局项、不销毁画面，按新路数重新排位。
  while (QLayoutItem* item = video_grid_->takeAt(0)) {
    delete item;
  }
  for (int col = 0; col < 4; ++col) {
    video_grid_->setColumnStretch(col, 0);
  }
  for (int row = 0; row < 16; ++row) {
    video_grid_->setRowStretch(row, 0);
  }
  const int count = video_views_.size();
  // 四路以内 2x2，再多到九路 3x3；后续要扩人数只需继续加列。
  const int columns = count <= 1 ? 1 : (count <= 4 ? 2 : (count <= 9 ? 3 : 4));
  int index = 0;
  for (VideoView* view : video_views_) {
    video_grid_->addWidget(view, index / columns, index % columns);
    ++index;
  }
  for (int col = 0; col < columns; ++col) {
    video_grid_->setColumnStretch(col, 1);
  }
  for (int row = 0; row * columns < count; ++row) {
    video_grid_->setRowStretch(row, 1);
  }
  video_stack_->setCurrentWidget(count > 0 ? video_grid_widget_
                                           : video_placeholder_);
}

void TalkWindow::RemoveVideoView(const QString& mid, const QString& reason) {
  const auto it = video_views_.find(mid);
  if (it == video_views_.end()) {
    return;
  }
  AppendLog(QStringLiteral("移除远端视频 mid=%1：%2").arg(mid, reason));
  it.value()->deleteLater();
  video_last_frame_ms_.remove(mid);
  video_tracks_.remove(mid);
  video_views_.erase(it);
  RelayoutVideoGrid();
}

void TalkWindow::PruneVideoViews() {
  // 服务端 close producer 不会有轨道移除回调，media.producer 又可能丢失
  // （断线重连期间），保留无帧超时作为兜底。
  constexpr qint64 kIdleTimeoutMs = 5000;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  QStringList stale;
  for (auto it = video_views_.begin(); it != video_views_.end(); ++it) {
    const qint64 last = video_last_frame_ms_.value(it.key(), 0);
    if (now - last > kIdleTimeoutMs) {
      stale.append(it.key());
    }
  }
  const QString reason =
      QStringLiteral("超过 %1 秒无帧").arg(kIdleTimeoutMs / 1000);
  for (const QString& mid : stale) {
    RemoveVideoView(mid, reason);
  }
}

// media.producer：{uid, mid, kind, source, paused}。发布方暂停某一路后，
// 订阅端不必再等 5 秒无帧超时，直接收掉那一路画面；恢复时不主动建视图，
// 等第一帧到达自然重建。
void TalkWindow::HandleProducerState(const QJsonObject& data) {
  const QString uid = data["uid"].toString();
  const QString kind = data["kind"].toString();
  const QString source = data["source"].toString();
  const bool paused = data["paused"].toBool();
  if (kind != QStringLiteral("video")) {
    // 音频发布状态不影响画面，直接忽略。
    return;
  }
  const QString source_name = source == "screen" ? QStringLiteral("屏幕共享")
                                                   : QStringLiteral("摄像头");
  AppendLog(QStringLiteral("对端 %1 的%2%3")
                .arg(uid, source_name,
                     paused ? QStringLiteral("已暂停") : QStringLiteral("已恢复")));
  if (!paused) {
    return;
  }
  // 通知里带的是发布方 mid，订阅端用的是 consumer mid，只能按 uid+source 匹配。
  QStringList mids;
  for (auto it = video_tracks_.begin(); it != video_tracks_.end(); ++it) {
    if (it.value()["uid"].toString() == uid &&
        it.value()["source"].toString() == source) {
      mids.append(it.key());
    }
  }
  for (const QString& mid : mids) {
    RemoveVideoView(mid, QStringLiteral("对端停止%1").arg(source_name));
  }
}

void TalkWindow::ClearVideoViews() {
  for (VideoView* view : video_views_) {
    view->deleteLater();
  }
  video_views_.clear();
  video_last_frame_ms_.clear();
  video_tracks_.clear();
  RelayoutVideoGrid();
}

void TalkWindow::HandleMediaError(const QString& name,
                                  const QJsonObject& data) {
  QString text = data["msg"].toString();
  const int code = data["code"].toInt();
  if (code > 0) {
    text += QStringLiteral("（错误码 %1）").arg(code);
  }
  const QString detail = data["detail"].toString();
  if (!detail.isEmpty()) {
    text += QStringLiteral("：%1").arg(detail);
  }
  AppendLog(QStringLiteral("%1：%2").arg(name, text));
}
