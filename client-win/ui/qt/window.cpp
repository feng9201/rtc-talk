// File: ui/qt/window.cpp
// Brief: 实现 Qt 主窗口的控件、命令与事件处理。
#include "window.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
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
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>

#include "core_client.h"

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
  resize(800, 600);
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
      QStringLiteral("系统音采集默认播放设备；进房前选择，入房后需重连生效"));

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
  auto* buttons = new QHBoxLayout;
  for (auto* button : {connect_, disconnect_, join_, leave_, mute_}) {
    buttons->addWidget(button);
  }
  layout->addLayout(buttons);

  state_ = new QLabel(QStringLiteral("未连接"));
  media_ = new QLabel(QStringLiteral("媒体：未建立"));
  members_ = new QListWidget;
  log_ = new QPlainTextEdit;
  log_->setReadOnly(true);
  log_->setMaximumBlockCount(100);
  log_->setMaximumHeight(110);
  layout->addWidget(state_);
  layout->addWidget(media_);
  layout->addWidget(members_);
  layout->addWidget(log_);

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
            settings_.audio_source = source_->currentData().toString();
            settings_.Save();
            UpdateSourceControls();
            RefreshDevices();
            if (connect_->isEnabled()) {
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
  }
  if (!core_->Start(CoreConfig())) {
    AppendLog(QStringLiteral("创建核心库失败"));
    return false;
  }
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
  source_->setEnabled(false);
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
  source_->setEnabled(audio && editable);
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
  devices_->setEnabled(true);
  backend_->setEnabled(true);
  mode_->setEnabled(true);
  const bool audio = backend_->currentData().toString() == "wasapi";
  source_->setEnabled(audio);
  playout_->setEnabled(audio);
  UpdateSourceControls();
  muted_ = true;
  mute_->setText(QStringLiteral("开麦"));
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
  if (name == "left" || name == "room.closed") {
    peers_.clear();
    RefreshMembers();
    join_->setEnabled(true);
    leave_->setEnabled(false);
    mute_->setEnabled(false);
    muted_ = true;
    mute_->setText(QStringLiteral("开麦"));
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
  if (method == "media.stats" && ok) {
    AppendLog(QString::fromUtf8(QJsonDocument(data["data"].toObject())
                                    .toJson(QJsonDocument::Compact)));
  }
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
