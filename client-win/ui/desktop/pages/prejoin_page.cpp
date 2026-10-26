#include "pages/prejoin_page.h"

#include "theme/theme.h"
#include "video/video_surface.h"
#include "widgets/pill.h"
#include "widgets/segmented_control.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QSysInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QVBoxLayout>

namespace talk::desktop {
namespace {

QString ComboData(const QComboBox* combo, const QString& fallback) {
  return combo != nullptr && combo->currentIndex() >= 0
             ? combo->currentData().toString()
             : fallback;
}

QLabel* Label(const QString& text, const QString& object_name,
              QWidget* parent = nullptr) {
  return Theme::label(text, object_name, parent);
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

  RefreshSummary();
  setJoinEnabled(true);
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
  titles->addWidget(Label(QStringLiteral("RTC Talk"), QStringLiteral("AppName"),
                          header));
  titles->addWidget(Label(QStringLiteral("生产桌面客户端 · Windows"),
                          QStringLiteral("AppSubtitle"), header));
  layout->addLayout(titles);
  layout->addStretch(1);
  layout->addWidget(new Pill(QStringLiteral("1 连接配置"),
                             QStringLiteral("stepActive"), header));
  layout->addWidget(new Pill(QStringLiteral("2 房间"), QStringLiteral("step"),
                             header));
  return header;
}

QWidget* PrejoinPage::BuildFormCard() {
  auto* card = Theme::card(this);
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
  url_->setPlaceholderText(
      QStringLiteral("ws://server:8080/talk/v1/ws"));
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
  audio_backend_->addItem(QStringLiteral("WASAPI（Core Audio）"), "wasapi");
  audio_backend_->addItem(QStringLiteral("合成音频（测试）"), "fake");
  audio_backend_->addItem(QStringLiteral("仅信令"), "none");
  media_mode_ = new QComboBox(card);
  media_mode_->addItem(QStringLiteral("自动（两人 P2P）"), "auto");
  media_mode_->addItem(QStringLiteral("仅 SFU"), "sfu");
  media_mode_->addItem(QStringLiteral("强制 P2P"), "p2p");
  talk_mode_ = new QComboBox(card);
  talk_mode_->addItem(QStringLiteral("自由发言"), "free");
  talk_mode_->addItem(QStringLiteral("按住说话 PTT"), "ptt");
  talk_mode_->addItem(QStringLiteral("主持人审核"), "moderated");
  layout->addWidget(MakePair(
      MakeField(QStringLiteral("音频后端"), audio_backend_),
      MakeField(QStringLiteral("媒体模式"), media_mode_)));
  layout->addWidget(MakeField(QStringLiteral("房间对讲模式"), talk_mode_));

  audio_source_ = new SegmentedControl(false, card);
  audio_source_->addItem(QStringLiteral("mic"), QStringLiteral("麦克风"));
  audio_source_->addItem(QStringLiteral("system"), QStringLiteral("系统音频"));
  audio_source_->setCurrentId(QStringLiteral("mic"));
  layout->addWidget(Label(QStringLiteral("音频源"),
                          QStringLiteral("SectionTitle"), card));
  layout->addWidget(audio_source_);

  recording_device_ = new QComboBox(card);
  recording_device_->addItem(QStringLiteral("默认通信设备"), QString());
  recording_device_->setEnabled(false);
  playout_device_ = new QComboBox(card);
  playout_device_->addItem(QStringLiteral("默认播放设备"), QString());
  playout_device_->setEnabled(false);
  camera_device_ = new QComboBox(card);
  camera_device_->addItem(QStringLiteral("正在扫描摄像头…"), -1);
  camera_device_->setEnabled(false);
  layout->addWidget(MakePair(
      MakeField(QStringLiteral("麦克风设备"), recording_device_,
                QStringLiteral("音频设备完整枚举在 P6 接入")),
      MakeField(QStringLiteral("播放设备"), playout_device_,
                QStringLiteral("音频设备完整枚举在 P6 接入"))));
  auto* camera_row = new QWidget(card);
  auto* camera_row_layout = new QHBoxLayout(camera_row);
  camera_row_layout->setContentsMargins(0, 0, 0, 0);
  camera_row_layout->setSpacing(8);
  camera_row_layout->addWidget(camera_device_, 1);
  auto* rescan = Theme::button(QStringLiteral("重新扫描"),
                               QStringLiteral("ghost"), camera_row);
  camera_row_layout->addWidget(rescan);
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
  save_ = Theme::button(QStringLiteral("保存设置"),
                        QStringLiteral("default"), card);
  join_ = Theme::button(QStringLiteral("进入房间"),
                        QStringLiteral("primary"), card);
  actions->addWidget(save_);
  actions->addStretch(1);
  actions->addWidget(join_);
  layout->addLayout(actions);

  connect(volume_, &QSlider::valueChanged, this, [this](int value) {
    if (volume_value_ != nullptr) {
      volume_value_->setText(QStringLiteral("%1%").arg(value));
    }
  });
  connect(join_, &QPushButton::clicked, this, [this] {
    emit joinRequested(options());
  });
  connect(save_, &QPushButton::clicked, this, [this] {
    emit saveSettingsRequested(options());
  });
  connect(audio_source_, &SegmentedControl::currentIdChanged, this,
          [this](const QString&) { RefreshSummary(); });
  connect(talk_mode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this] { RefreshSummary(); });
  connect(rescan, &QPushButton::clicked, this,
          &PrejoinPage::cameraRefreshRequested);
  connect(camera_device_,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this] {
            if (preview_active_) {
              emit cameraPreviewRequested(camera_device_->currentData().toInt());
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

  auto* preview_card = Theme::card(column);
  auto* preview_layout = new QVBoxLayout(preview_card);
  preview_layout->setContentsMargins(14, 14, 14, 14);
  preview_layout->setSpacing(12);

  preview_surface_ = CreateImageVideoSurface(preview_card);
  preview_surface_->setObjectName(QStringLiteral("PreviewSurface"));
  preview_surface_->setMinimumHeight(310);
  if (auto* image = qobject_cast<ImageVideoSurface*>(preview_surface_)) {
    image->SetMirrored(true);
    image->SetPlaceholder(QStringLiteral("摄像头预览已关闭"));
  }
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
  preview_ = Theme::button(QStringLiteral("启动本地预览"),
                           QStringLiteral("default"), preview_card);
  preview_->setEnabled(false);
  identity->addWidget(preview_);
  preview_layout->addLayout(identity);
  layout->addWidget(preview_card, 1);

  auto* summary = Theme::card(column);
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
  connect(preview_, &QPushButton::clicked, this,
          &PrejoinPage::previewToggleRequested);
  return column;
}

QWidget* PrejoinPage::MakeField(const QString& text, QWidget* editor,
                                const QString& hint) {
  auto* field = new QWidget(this);
  auto* layout = new QVBoxLayout(field);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(5);
  layout->addWidget(Label(text, QStringLiteral("FieldLabel"), field));
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

PrejoinOptions PrejoinPage::options() const {
  PrejoinOptions result;
  result.url = url_->text().trimmed();
  result.userId = user_id_->text().trimmed();
  result.roomId = room_id_->text().trimmed();
  result.token = token_->text();
  result.fetchDevToken = dev_token_->isChecked();

  const QString backend = ComboData(audio_backend_, QStringLiteral("wasapi"));
  result.audioBackend = backend == QStringLiteral("none")
                            ? AudioBackend::None
                            : backend == QStringLiteral("fake")
                                  ? AudioBackend::Fake
                                  : AudioBackend::Wasapi;
  const QString mode = ComboData(media_mode_, QStringLiteral("auto"));
  result.mediaMode = mode == QStringLiteral("p2p")
                         ? MediaModePreference::P2p
                         : mode == QStringLiteral("sfu")
                               ? MediaModePreference::Sfu
                               : MediaModePreference::Auto;
  const QString talk = ComboData(talk_mode_, QStringLiteral("free"));
  result.talkMode = talk == QStringLiteral("ptt")
                        ? TalkMode::Ptt
                        : talk == QStringLiteral("moderated")
                              ? TalkMode::Moderated
                              : TalkMode::Free;
  result.audioSource = audio_source_->currentId() == QStringLiteral("system")
                           ? AudioSource::System
                           : AudioSource::Microphone;
  result.volume = volume_->value();
  result.recordingDevice = recording_device_->currentData().toString();
  result.playoutDevice = playout_device_->currentData().toString();
  result.cameraDevice = camera_device_->currentData().toString();
  result.cameraIndex = camera_device_->currentData().toInt();
  return result;
}

void PrejoinPage::SetOptions(const PrejoinOptions& options) {
  url_->setText(options.url);
  user_id_->setText(options.userId);
  room_id_->setText(options.roomId);
  token_->clear();
  dev_token_->setChecked(options.fetchDevToken);

  const auto set_combo = [](QComboBox* combo, const QString& value) {
    const int index = combo->findData(value);
    if (index >= 0) combo->setCurrentIndex(index);
  };
  set_combo(audio_backend_,
            options.audioBackend == AudioBackend::None
                ? QStringLiteral("none")
                : options.audioBackend == AudioBackend::Fake
                      ? QStringLiteral("fake")
                      : QStringLiteral("wasapi"));
  set_combo(media_mode_,
            options.mediaMode == MediaModePreference::P2p
                ? QStringLiteral("p2p")
                : options.mediaMode == MediaModePreference::Sfu
                      ? QStringLiteral("sfu")
                      : QStringLiteral("auto"));
  set_combo(talk_mode_,
            options.talkMode == TalkMode::Ptt
                ? QStringLiteral("ptt")
                : options.talkMode == TalkMode::Moderated
                      ? QStringLiteral("moderated")
                      : QStringLiteral("free"));
  audio_source_->setCurrentId(options.audioSource == AudioSource::System
                                  ? QStringLiteral("system")
                                  : QStringLiteral("mic"));
  volume_->setValue(options.volume);
  preferred_camera_index_ = options.cameraIndex;
  const int camera_index = camera_device_->findData(options.cameraIndex);
  if (camera_index >= 0) camera_device_->setCurrentIndex(camera_index);
  RefreshSummary();
}

void PrejoinPage::setJoinEnabled(bool enabled) {
  if (join_ != nullptr) join_->setEnabled(enabled);
}

void PrejoinPage::setStatusMessage(const QString& text, const QString& tone) {
  if (status_ == nullptr) return;
  status_->setText(text);
  status_->setProperty("tone", tone);
  status_->style()->unpolish(status_);
  status_->style()->polish(status_);
}

void PrejoinPage::SetCameraDevices(const QVector<CameraDevice>& devices) {
  if (camera_device_ == nullptr) return;
  const QSignalBlocker blocker(camera_device_);
  const int current =
      preferred_camera_index_ >= 0
          ? preferred_camera_index_
          : camera_device_->currentData().toInt();
  camera_device_->clear();
  if (devices.isEmpty()) {
    camera_device_->addItem(QStringLiteral("未找到可用摄像头"), -1);
    camera_device_->setEnabled(false);
    preview_->setEnabled(false);
    preview_device_->setText(QStringLiteral("未找到可用摄像头"));
    return;
  }
  for (const CameraDevice& device : devices) {
    camera_device_->addItem(device.name, device.index);
  }
  int index = camera_device_->findData(current);
  if (index < 0) {
    index = camera_device_->findData(preferred_camera_index_);
  }
  if (index < 0) index = 0;
  camera_device_->setCurrentIndex(index);
  camera_device_->setEnabled(true);
  if (preview_ != nullptr) preview_->setEnabled(true);
  if (preview_device_ != nullptr && !preview_active_) {
    preview_device_->setText(camera_device_->currentText());
  }
}

void PrejoinPage::SetPreviewFrame(const QImage& frame) {
  if (preview_surface_ == nullptr) return;
  if (frame.isNull()) {
    preview_surface_->ClearFrame();
  } else {
    preview_surface_->SetFrame(frame);
  }
}

void PrejoinPage::SetPreviewActive(bool active, const QString& device) {
  preview_active_ = active;
  if (preview_ != nullptr) {
    preview_->setText(active ? QStringLiteral("关闭本地预览")
                             : QStringLiteral("启动本地预览"));
  }
  if (preview_device_ != nullptr) {
    preview_device_->setText(
        active && !device.isEmpty() ? device
                                   : camera_device_->currentText());
  }
  if (!active) SetPreviewFrame(QImage());
}

void PrejoinPage::RefreshSummary() {
  if (summary_connection_ == nullptr) return;
  summary_connection_->setText(QStringLiteral("未连接"));
  summary_audio_->setText(
      QStringLiteral("%1 · 48 kHz / 32 kbps")
          .arg(audio_source_->currentId() == QStringLiteral("system")
                   ? QStringLiteral("系统音")
                   : QStringLiteral("麦克风")));
  summary_video_->setText(QStringLiteral("H.264 · 支持本地预览"));
  summary_policy_->setText(talk_mode_->currentText());
}

}  // namespace talk::desktop
