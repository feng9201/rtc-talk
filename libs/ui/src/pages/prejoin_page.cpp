#include "rtcwin/ui/pages/prejoin_page.h"

#include "rtcwin/muxplayer_adapter/mux_video_surface.h"
#include "rtcwin/ui/theme/theme.h"
#include "rtcwin/ui/widgets/pill.h"
#include "rtcwin/ui/widgets/segmented_control.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QSysInfo>
#include <QVBoxLayout>

namespace rtcwin::ui {
namespace {

QString Text(const std::string& value) {
  return QString::fromStdString(value);
}

std::string Value(const QString& value) {
  return value.toStdString();
}

QString ComboData(const QComboBox* combo, const QString& fallback) {
  return combo != nullptr && combo->currentIndex() >= 0
             ? combo->currentData().toString()
             : fallback;
}

QLabel* Label(const QString& text, const QString& object_name,
              QWidget* parent = nullptr) {
  return theme::Label(text, object_name, parent);
}

}  // namespace

PrejoinPage::PrejoinPage(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("PrejoinPage"));

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);
  root->addWidget(BuildHeader());

  auto* scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* content = new QWidget(scroll);
  content->setObjectName(QStringLiteral("PrejoinContent"));
  auto* content_layout = new QHBoxLayout(content);
  content_layout->setContentsMargins(26, 22, 26, 26);
  content_layout->setSpacing(22);
  content_layout->addWidget(BuildFormCard(), 64);
  content_layout->addWidget(BuildPreviewColumn(), 36);
  scroll->setWidget(content);
  root->addWidget(scroll, 1);

  UpdateSummary();
  SetBusy(false);
}

QWidget* PrejoinPage::BuildHeader() {
  auto* header = new QFrame(this);
  header->setObjectName(QStringLiteral("AppHeader"));
  header->setFixedHeight(66);
  auto* layout = new QHBoxLayout(header);
  layout->setContentsMargins(22, 0, 126, 0);
  layout->setSpacing(12);

  auto* brand = new QLabel(QStringLiteral("RT"), header);
  brand->setObjectName(QStringLiteral("BrandMark"));
  brand->setAlignment(Qt::AlignCenter);
  brand->setFixedSize(42, 42);
  layout->addWidget(brand);

  auto* titles = new QVBoxLayout;
  titles->setContentsMargins(0, 0, 0, 0);
  titles->setSpacing(1);
  titles->addWidget(Label(QStringLiteral("RTC Talk"),
                          QStringLiteral("AppName"), header));
  titles->addWidget(Label(QStringLiteral("生产桌面客户端 · Windows"),
                          QStringLiteral("AppSubtitle"), header));
  layout->addLayout(titles);
  layout->addStretch(1);
  layout->addWidget(new Pill(QStringLiteral("1 连接配置"),
                             QStringLiteral("stepActive"), header));
  layout->addWidget(new Pill(QStringLiteral("2 房间"),
                             QStringLiteral("step"), header));
  return header;
}

QWidget* PrejoinPage::BuildFormCard() {
  auto* card = theme::Card(this);
  auto* layout = new QVBoxLayout(card);
  layout->setContentsMargins(22, 20, 22, 20);
  layout->setSpacing(13);

  auto* title_row = new QHBoxLayout;
  auto* titles = new QVBoxLayout;
  titles->setContentsMargins(0, 0, 0, 0);
  titles->setSpacing(2);
  titles->addWidget(Label(QStringLiteral("连接与入房"),
                          QStringLiteral("Title"), card));
  titles->addWidget(Label(
      QStringLiteral("连接真实 talk-core，支持摄像头选择与入房前本地预览。"),
      QStringLiteral("Subtitle"), card));
  title_row->addLayout(titles, 1);
  title_row->addWidget(new Pill(QStringLiteral("P5 MEDIA"),
                                QStringLiteral("accent"), card),
                       0, Qt::AlignTop);
  layout->addLayout(title_row);

  url_ = new QLineEdit(card);
  url_->setPlaceholderText(QStringLiteral("ws://server:8080/talk/v1/ws"));
  user_id_ = new QLineEdit(
      QStringLiteral("windows-") + QSysInfo::machineHostName().toLower(), card);
  room_id_ = new QLineEdit(card);
  room_id_->setPlaceholderText(QStringLiteral("输入房间 ID"));
  token_ = new QLineEdit(card);
  token_->setEchoMode(QLineEdit::Password);
  token_->setPlaceholderText(QStringLiteral("仅保存在内存，不写入配置"));
  dev_token_ = new QCheckBox(QStringLiteral("开发测试：从 /dev/token 获取凭证"),
                             card);

  layout->addWidget(MakeField(QStringLiteral("信令服务"), url_));
  layout->addWidget(MakePair(
      MakeField(QStringLiteral("用户标识"), user_id_),
      MakeField(QStringLiteral("房间 ID"), room_id_)));
  layout->addWidget(MakeField(QStringLiteral("Token"), token_));
  layout->addWidget(dev_token_);

  audio_backend_ = new QComboBox(card);
  audio_backend_->addItem(QStringLiteral("WASAPI（Core Audio）"),
                          QStringLiteral("wasapi"));
  audio_backend_->addItem(QStringLiteral("合成音频（测试）"),
                          QStringLiteral("fake"));
  audio_backend_->addItem(QStringLiteral("仅信令"), QStringLiteral("none"));
  media_mode_ = new QComboBox(card);
  media_mode_->addItem(QStringLiteral("自动（两人 P2P）"),
                       QStringLiteral("auto"));
  media_mode_->addItem(QStringLiteral("仅 SFU"), QStringLiteral("sfu"));
  media_mode_->addItem(QStringLiteral("强制 P2P"), QStringLiteral("p2p"));
  talk_mode_ = new QComboBox(card);
  talk_mode_->addItem(QStringLiteral("自由发言"), QStringLiteral("free"));
  talk_mode_->addItem(QStringLiteral("按住说话 PTT"), QStringLiteral("ptt"));
  talk_mode_->addItem(QStringLiteral("主持人审核"),
                      QStringLiteral("moderated"));
  layout->addWidget(MakePair(
      MakeField(QStringLiteral("音频后端"), audio_backend_),
      MakeField(QStringLiteral("媒体模式"), media_mode_)));
  layout->addWidget(MakeField(QStringLiteral("房间对讲模式"), talk_mode_));

  audio_source_ = new SegmentedControl(false, card);
  audio_source_->AddItem(QStringLiteral("mic"), QStringLiteral("麦克风"));
  audio_source_->AddItem(QStringLiteral("system"), QStringLiteral("系统音频"));
  audio_source_->SetCurrentId(QStringLiteral("mic"));
  layout->addWidget(Label(QStringLiteral("音频源"),
                          QStringLiteral("SectionTitle"), card));
  layout->addWidget(audio_source_);

  recording_device_ = new QComboBox(card);
  recording_device_->addItem(QStringLiteral("默认通信设备"), QString());
  recording_device_->setEnabled(false);
  playout_device_ = new QComboBox(card);
  playout_device_->addItem(QStringLiteral("默认播放设备"), QString());
  playout_device_->setEnabled(false);
  system_audio_device_ = new QComboBox(card);
  system_audio_device_->addItem(QStringLiteral("默认播放端点"), QString());
  system_audio_device_->setEnabled(false);
  camera_device_ = new QComboBox(card);
  camera_device_->addItem(QStringLiteral("正在扫描摄像头…"), -1);
  camera_device_->setEnabled(false);
  layout->addWidget(MakePair(
      MakeField(QStringLiteral("麦克风设备"), recording_device_,
                QStringLiteral("WASAPI 设备由媒体引擎枚举")),
      MakeField(QStringLiteral("播放设备"), playout_device_,
                QStringLiteral("WASAPI 设备由媒体引擎枚举"))));
  layout->addWidget(MakeField(
      QStringLiteral("系统音采集端点"), system_audio_device_,
      QStringLiteral("系统音模式使用该播放端点做 loopback；留空跟随默认设备")));
  auto* camera_row = new QWidget(card);
  auto* camera_row_layout = new QHBoxLayout(camera_row);
  camera_row_layout->setContentsMargins(0, 0, 0, 0);
  camera_row_layout->setSpacing(8);
  camera_row_layout->addWidget(camera_device_, 1);
  rescan_button_ = theme::Button(QStringLiteral("重新扫描"),
                                 QStringLiteral("ghost"), camera_row);
  camera_row_layout->addWidget(rescan_button_);
  layout->addWidget(MakeField(QStringLiteral("摄像头"), camera_row,
                              QStringLiteral("设备索引会随入房配置传给媒体核心")));

  auto* volume_row = new QHBoxLayout;
  volume_row->setContentsMargins(0, 0, 0, 0);
  volume_row->setSpacing(10);
  volume_ = new QSlider(Qt::Horizontal, card);
  volume_->setRange(0, 100);
  volume_->setValue(60);
  volume_value_ = Label(QStringLiteral("60%"), QStringLiteral("Value"), card);
  volume_value_->setFixedWidth(42);
  volume_row->addWidget(volume_, 1);
  volume_row->addWidget(volume_value_);
  layout->addWidget(Label(QStringLiteral("播放音量"),
                          QStringLiteral("SectionTitle"), card));
  layout->addLayout(volume_row);

  auto* actions = new QHBoxLayout;
  actions->setContentsMargins(0, 6, 0, 0);
  actions->setSpacing(10);
  save_button_ = theme::Button(QStringLiteral("保存设置"),
                               QStringLiteral("default"), card);
  join_button_ = theme::Button(QStringLiteral("进入房间"),
                               QStringLiteral("primary"), card);
  actions->addWidget(save_button_);
  actions->addStretch(1);
  actions->addWidget(join_button_);
  layout->addLayout(actions);

  connect(volume_, &QSlider::valueChanged, this, [this](int value) {
    if (volume_value_ != nullptr) {
      volume_value_->setText(QStringLiteral("%1%").arg(value));
    }
  });
  connect(join_button_, &QPushButton::clicked, this, [this] {
    emit JoinRequested(Options());
  });
  connect(save_button_, &QPushButton::clicked, this, [this] {
    emit SaveRequested(Options());
  });
  connect(audio_source_, &SegmentedControl::CurrentIdChanged, this,
          [this](const QString&) { UpdateSummary(); });
  connect(talk_mode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this] { UpdateSummary(); });
  connect(rescan_button_, &QPushButton::clicked, this,
          &PrejoinPage::CameraRefreshRequested);
  connect(camera_device_,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this] {
            const int index = SelectedCameraIndex();
            if (index >= 0 && !preview_pending_) {
              if (preview_active_) {
                SetPreviewPending(QStringLiteral("正在切换摄像头…"));
              }
              emit CameraSelectionChanged(index);
            }
          });
  return card;
}

QWidget* PrejoinPage::BuildPreviewColumn() {
  auto* column = new QWidget(this);
  column->setMinimumWidth(360);
  auto* layout = new QVBoxLayout(column);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(16);

  auto* preview_card = theme::Card(column);
  auto* preview_layout = new QVBoxLayout(preview_card);
  preview_layout->setContentsMargins(14, 14, 14, 14);
  preview_layout->setSpacing(12);

  preview_surface_ = new rtcwin::muxplayer_adapter::MuxVideoSurface(preview_card);
  preview_surface_->setObjectName(QStringLiteral("PreviewSurface"));
  preview_surface_->setMinimumHeight(310);
  preview_surface_->SetPlaceholder(QStringLiteral("摄像头预览已关闭"));
  preview_layout->addWidget(preview_surface_, 1);

  auto* identity = new QHBoxLayout;
  auto* identity_text = new QVBoxLayout;
  identity_text->setSpacing(2);
  identity_text->addWidget(Label(QStringLiteral("本机身份"),
                                 QStringLiteral("TitleSmall"), preview_card));
  preview_device_ = Label(QStringLiteral("选择摄像头后可启动预览"),
                          QStringLiteral("Hint"), preview_card);
  identity_text->addWidget(preview_device_);
  identity->addLayout(identity_text, 1);
  preview_button_ = theme::Button(QStringLiteral("启动本地预览"),
                                  QStringLiteral("default"), preview_card);
  preview_button_->setEnabled(false);
  identity->addWidget(preview_button_);
  preview_layout->addLayout(identity);
  layout->addWidget(preview_card, 1);

  auto* summary = theme::Card(column);
  auto* summary_layout = new QVBoxLayout(summary);
  summary_layout->setContentsMargins(16, 14, 16, 14);
  summary_layout->setSpacing(9);
  summary_layout->addWidget(Label(QStringLiteral("本次入房"),
                                  QStringLiteral("SectionTitle"), summary));
  const auto add_summary = [summary, summary_layout](const QString& name) {
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(Label(name, QStringLiteral("Muted"), summary), 1);
    auto* value = Label(QString(), QStringLiteral("SummaryValue"), summary);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(value);
    summary_layout->addLayout(row);
    return value;
  };
  summary_connection_ = add_summary(QStringLiteral("连接状态"));
  summary_audio_ = add_summary(QStringLiteral("音频"));
  summary_video_ = add_summary(QStringLiteral("视频"));
  summary_policy_ = add_summary(QStringLiteral("房间模式"));
  status_ = Label(QStringLiteral("正在枚举媒体设备…"),
                  QStringLiteral("Hint"), summary);
  status_->setWordWrap(true);
  summary_layout->addWidget(status_);
  layout->addWidget(summary);

  connect(preview_button_, &QPushButton::clicked, this,
          &PrejoinPage::TogglePreview);
  return column;
}

QWidget* PrejoinPage::MakeField(const QString& label, QWidget* editor,
                                const QString& hint) {
  auto* field = new QWidget(this);
  auto* layout = new QVBoxLayout(field);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(5);
  layout->addWidget(Label(label, QStringLiteral("FieldLabel"), field));
  layout->addWidget(editor);
  if (!hint.isEmpty()) {
    layout->addWidget(Label(hint, QStringLiteral("Hint"), field));
  }
  return field;
}

QWidget* PrejoinPage::MakePair(QWidget* left, QWidget* right) {
  auto* pair = new QWidget(this);
  auto* layout = new QHBoxLayout(pair);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);
  layout->addWidget(left, 1);
  layout->addWidget(right, 1);
  return pair;
}

domain::PrejoinOptions PrejoinPage::Options() const {
  domain::PrejoinOptions result;
  result.signaling_url = Value(url_->text().trimmed());
  result.user_id = Value(user_id_->text().trimmed());
  result.room_id = Value(room_id_->text().trimmed());
  result.token = Value(token_->text());
  result.fetch_dev_token = dev_token_->isChecked();

  const QString backend = ComboData(audio_backend_, QStringLiteral("wasapi"));
  result.audio_backend = backend == QStringLiteral("none")
                             ? domain::AudioBackend::None
                             : backend == QStringLiteral("fake")
                                   ? domain::AudioBackend::Fake
                                   : domain::AudioBackend::Wasapi;
  const QString mode = ComboData(media_mode_, QStringLiteral("auto"));
  result.media_mode = mode == QStringLiteral("p2p")
                          ? domain::MediaModePreference::P2p
                          : mode == QStringLiteral("sfu")
                                ? domain::MediaModePreference::Sfu
                                : domain::MediaModePreference::Auto;
  const QString talk = ComboData(talk_mode_, QStringLiteral("free"));
  result.talk_mode = talk == QStringLiteral("ptt")
                         ? domain::TalkMode::Ptt
                         : talk == QStringLiteral("moderated")
                               ? domain::TalkMode::Moderated
                               : domain::TalkMode::Free;
  result.audio_source =
      audio_source_->CurrentId() == QStringLiteral("system")
          ? domain::AudioSource::System
          : domain::AudioSource::Microphone;
  result.volume = volume_->value();
  result.recording_device = Value(ComboData(recording_device_, QString()));
  result.playout_device = Value(ComboData(playout_device_, QString()));
  result.system_audio_device =
      Value(ComboData(system_audio_device_, QString()));
  result.camera_device = Value(QString::number(SelectedCameraIndex()));
  return result;
}

void PrejoinPage::SetOptions(const domain::PrejoinOptions& options) {
  if (!options.signaling_url.empty()) url_->setText(Text(options.signaling_url));
  if (!options.user_id.empty()) user_id_->setText(Text(options.user_id));
  if (!options.room_id.empty()) room_id_->setText(Text(options.room_id));
  token_->clear();
  dev_token_->setChecked(options.fetch_dev_token);

  const auto set_combo = [](QComboBox* combo, const QString& value) {
    const int index = combo->findData(value);
    if (index >= 0) combo->setCurrentIndex(index);
  };
  set_combo(audio_backend_,
            options.audio_backend == domain::AudioBackend::None
                ? QStringLiteral("none")
                : options.audio_backend == domain::AudioBackend::Fake
                      ? QStringLiteral("fake")
                      : QStringLiteral("wasapi"));
  set_combo(media_mode_,
            options.media_mode == domain::MediaModePreference::P2p
                ? QStringLiteral("p2p")
                : options.media_mode == domain::MediaModePreference::Sfu
                      ? QStringLiteral("sfu")
                      : QStringLiteral("auto"));
  set_combo(talk_mode_,
            options.talk_mode == domain::TalkMode::Ptt
                ? QStringLiteral("ptt")
                : options.talk_mode == domain::TalkMode::Moderated
                      ? QStringLiteral("moderated")
                      : QStringLiteral("free"));
  audio_source_->SetCurrentId(
      options.audio_source == domain::AudioSource::System
          ? QStringLiteral("system")
          : QStringLiteral("mic"));
  volume_->setValue(options.volume);
  preferred_camera_id_ = Text(options.camera_device);
  preferred_recording_id_ = Text(options.recording_device);
  preferred_playout_id_ = Text(options.playout_device);
  preferred_system_audio_id_ = Text(options.system_audio_device);
  const int recording_index = recording_device_->findData(preferred_recording_id_);
  if (recording_index >= 0) recording_device_->setCurrentIndex(recording_index);
  const int playout_index = playout_device_->findData(preferred_playout_id_);
  if (playout_index >= 0) playout_device_->setCurrentIndex(playout_index);
  const int system_audio_index =
      system_audio_device_->findData(preferred_system_audio_id_);
  if (system_audio_index >= 0) system_audio_device_->setCurrentIndex(
      system_audio_index);
  bool has_index = false;
  const int preferred_index = preferred_camera_id_.toInt(&has_index);
  if (has_index) {
    const int index = camera_device_->findData(preferred_index);
    if (index >= 0) camera_device_->setCurrentIndex(index);
  }
  UpdateSummary();
}

void PrejoinPage::SetBusy(bool busy) {
  busy_ = busy;
  if (join_button_ != nullptr) join_button_->setEnabled(!busy);
  if (save_button_ != nullptr) save_button_->setEnabled(!busy);
  UpdatePreviewControls();
}

void PrejoinPage::SetStatus(const QString& text, const QString& tone) {
  if (status_ == nullptr) return;
  status_->setText(text);
  status_->setProperty("tone", tone);
  status_->style()->unpolish(status_);
  status_->style()->polish(status_);
}

void PrejoinPage::SetAudioDevices(
    const QVector<rtcwin::domain::AudioDevice>& recording,
    const QVector<rtcwin::domain::AudioDevice>& playout) {
  const auto fill = [this](QComboBox* combo,
                           const QVector<rtcwin::domain::AudioDevice>& devices,
                           const QString& preferred,
                           const QString& empty_text) {
    const QSignalBlocker blocker(combo);
    combo->clear();
    for (const rtcwin::domain::AudioDevice& device : devices) {
      const QString id = Text(device.id);
      combo->addItem(Text(device.name), id);
    }
    if (combo->count() == 0) {
      combo->addItem(empty_text, QString());
      combo->setEnabled(false);
      return;
    }
    int index = combo->findData(preferred);
    if (index < 0) index = 0;
    combo->setCurrentIndex(index);
    combo->setEnabled(true);
  };
  fill(recording_device_, recording, preferred_recording_id_,
       QStringLiteral("未检测到麦克风"));
  fill(playout_device_, playout, preferred_playout_id_,
       QStringLiteral("未检测到播放设备"));
  fill(system_audio_device_, playout, preferred_system_audio_id_,
       QStringLiteral("未检测到系统音端点"));
  preferred_recording_id_ = recording_device_->currentData().toString();
  preferred_playout_id_ = playout_device_->currentData().toString();
  preferred_system_audio_id_ =
      system_audio_device_->currentData().toString();
}
void PrejoinPage::SetCameraDevices(
    const QVector<rtcwin::domain::CameraDevice>& devices) {
  if (camera_device_ == nullptr) return;

  const QSignalBlocker blocker(camera_device_);
  bool has_current = false;
  int current = camera_device_->currentData().toInt(&has_current);
  bool has_preferred = false;
  const int preferred = preferred_camera_id_.toInt(&has_preferred);
  if (has_preferred) current = preferred;

  camera_device_->clear();
  for (const rtcwin::domain::CameraDevice& device : devices) {
    bool is_index = false;
    const int index = Text(device.id).toInt(&is_index);
    if (!is_index || !device.available) continue;
    camera_device_->addItem(Text(device.name), index);
  }

  if (camera_device_->count() == 0) {
    camera_device_->addItem(QStringLiteral("未找到可用摄像头"), -1);
    camera_device_->setCurrentIndex(0);
    preview_device_->setText(QStringLiteral("未找到可用摄像头"));
    SetStatus(QStringLiteral("未找到可用摄像头"), QStringLiteral("warn"));
    UpdatePreviewControls();
    return;
  }

  int index = has_current ? camera_device_->findData(current) : -1;
  if (index < 0) index = 0;
  camera_device_->setCurrentIndex(index);
  preferred_camera_id_ = QString::number(camera_device_->currentData().toInt());
  if (!preview_active_) {
    preview_device_->setText(camera_device_->currentText());
  }
  SetStatus(QStringLiteral("已发现 %1 个摄像头").arg(camera_device_->count()),
            QStringLiteral("ok"));
  UpdatePreviewControls();
}

void PrejoinPage::SetPreviewPending(const QString& text) {
  preview_pending_ = true;
  preview_surface_->SetPlaceholder(text);
  SetStatus(text, QStringLiteral("info"));
  UpdatePreviewControls();
}

void PrejoinPage::SetPreviewActive(bool active, const QString& device,
                                   int width, int height, int fps) {
  Q_UNUSED(width);
  Q_UNUSED(height);
  Q_UNUSED(fps);
  preview_pending_ = false;
  preview_active_ = active;
  if (active) {
    preview_surface_->SetPlaceholder(QStringLiteral("等待 muxPlayer 本地预览帧"));
    preview_device_->setText(device.isEmpty() ? camera_device_->currentText()
                                              : device);
    SetStatus(QStringLiteral("本机摄像头预览已启动"), QStringLiteral("ok"));
  } else {
    preview_surface_->Clear();
    preview_surface_->SetPlaceholder(QStringLiteral("摄像头预览已关闭"));
    preview_device_->setText(SelectedCameraIndex() >= 0
                                 ? camera_device_->currentText()
                                 : QStringLiteral("未找到可用摄像头"));
    SetStatus(SelectedCameraIndex() >= 0
                  ? QStringLiteral("摄像头预览已关闭")
                  : QStringLiteral("未找到可用摄像头"),
              SelectedCameraIndex() >= 0 ? QString() : QStringLiteral("warn"));
  }
  UpdatePreviewControls();
}

void PrejoinPage::SetPreviewFrame(const domain::VideoFrame& frame) {
  if (!preview_active_) return;
  if (!preview_surface_->SubmitFrame(frame)) {
    SetStatus(QStringLiteral("muxPlayer 渲染帧失败"), QStringLiteral("danger"));
  }
}

int PrejoinPage::SelectedCameraIndex() const {
  if (camera_device_ == nullptr || camera_device_->currentIndex() < 0) return -1;
  bool ok = false;
  const int index = camera_device_->currentData().toInt(&ok);
  return ok ? index : -1;
}

void PrejoinPage::UpdatePreviewControls() {
  if (preview_button_ == nullptr) return;
  const bool has_device = SelectedCameraIndex() >= 0;
  const bool selectable = !busy_ && !preview_pending_;
  camera_device_->setEnabled(selectable && has_device);
  if (rescan_button_ != nullptr) rescan_button_->setEnabled(selectable);
  preview_button_->setText(preview_active_
                               ? QStringLiteral("关闭本地预览")
                               : QStringLiteral("启动本地预览"));
  preview_button_->setEnabled(selectable && (preview_active_ || has_device));
}

void PrejoinPage::TogglePreview() {
  if (busy_ || preview_pending_) return;
  const bool start = !preview_active_;
  if (start && SelectedCameraIndex() < 0) {
    SetStatus(QStringLiteral("未找到可用摄像头"), QStringLiteral("warn"));
    return;
  }
  SetPreviewPending(start ? QStringLiteral("正在启动摄像头…")
                          : QStringLiteral("正在关闭本地预览…"));
  emit PreviewToggleRequested(start);
}

void PrejoinPage::UpdateSummary() {
  if (summary_connection_ == nullptr) return;
  summary_connection_->setText(QStringLiteral("未连接"));
  summary_audio_->setText(
      QStringLiteral("%1 · 48 kHz / 32 kbps")
          .arg(audio_source_->CurrentId() == QStringLiteral("system")
                   ? QStringLiteral("系统音")
                   : QStringLiteral("麦克风")));
  summary_video_->setText(QStringLiteral("H.264 · 支持本地预览"));
  summary_policy_->setText(talk_mode_->currentText());
}

}  // namespace rtcwin::ui
