#include "pages/prejoin_page.h"

#include "theme/theme.h"
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
  setJoinEnabled(false);
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
      QStringLiteral("连接真实 talk-core；P1 仅搭建生产 UI 骨架。"),
      QStringLiteral("Subtitle"), card));
  title_row->addLayout(titles, 1);
  title_row->addWidget(new Pill(QStringLiteral("P1 SCAFFOLD"),
                                QStringLiteral("accent"), card),
                       0, Qt::AlignTop);
  layout->addLayout(title_row);

  url_ = new QLineEdit(QStringLiteral("wss://your-server.example/talk/v1/ws"),
                       card);
  url_->setPlaceholderText(QStringLiteral("wss://server/talk/v1/ws"));
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
  camera_device_->addItem(QStringLiteral("默认摄像头"), QString());
  camera_device_->setEnabled(false);
  layout->addWidget(MakePair(
      MakeField(QStringLiteral("麦克风设备"), recording_device_,
                QStringLiteral("设备枚举在 P2/P5 接入")),
      MakeField(QStringLiteral("播放设备"), playout_device_,
                QStringLiteral("设备枚举在 P2 接入"))));
  layout->addWidget(MakeField(QStringLiteral("摄像头"), camera_device_,
                              QStringLiteral("设备枚举在 P5 接入")));

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
  save_->setEnabled(false);
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

  auto* placeholder = new QFrame(preview_card);
  placeholder->setObjectName(QStringLiteral("PreviewPlaceholder"));
  placeholder->setMinimumHeight(310);
  auto* placeholder_layout = new QVBoxLayout(placeholder);
  placeholder_layout->setAlignment(Qt::AlignCenter);
  auto* icon = new QLabel(placeholder);
  icon->setPixmap(Theme::pixmap(Theme::Icon::CameraOff, 42,
                                QColor(QStringLiteral("#5c6b79"))));
  icon->setAlignment(Qt::AlignCenter);
  placeholder_layout->addWidget(icon);
  placeholder_layout->addWidget(
      Label(QStringLiteral("本机视频预览将在 P5 接入"),
            QStringLiteral("Muted"), placeholder),
      0, Qt::AlignHCenter);
  preview_layout->addWidget(placeholder, 1);

  auto* identity = new QHBoxLayout;
  auto* identity_text = new QVBoxLayout;
  identity_text->setSpacing(2);
  identity_text->addWidget(Label(QStringLiteral("本机身份"),
                                 QStringLiteral("TitleSmall"), preview_card));
  identity_text->addWidget(Label(QStringLiteral("尚未连接核心"),
                                 QStringLiteral("Hint"), preview_card));
  identity->addLayout(identity_text, 1);
  auto* preview = Theme::button(QStringLiteral("本地预览"),
                                QStringLiteral("default"), preview_card);
  preview->setEnabled(false);
  identity->addWidget(preview);
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
  status_ = Label(QStringLiteral("P1 生产骨架已就绪；真实核心接入从 P2 开始。"),
                  QStringLiteral("Hint"), summary);
  status_->setWordWrap(true);
  summary_layout->addWidget(status_);
  layout->addWidget(summary);
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
  return result;
}

void PrejoinPage::setJoinEnabled(bool enabled) {
  if (join_ != nullptr) join_->setEnabled(enabled);
  if (save_ != nullptr) save_->setEnabled(enabled);
}

void PrejoinPage::setStatusMessage(const QString& text, const QString& tone) {
  if (status_ == nullptr) return;
  status_->setText(text);
  status_->setProperty("tone", tone);
  status_->style()->unpolish(status_);
  status_->style()->polish(status_);
}

void PrejoinPage::RefreshSummary() {
  if (summary_connection_ == nullptr) return;
  summary_connection_->setText(QStringLiteral("未连接"));
  summary_audio_->setText(
      QStringLiteral("%1 · 48 kHz / 32 kbps")
          .arg(audio_source_->currentId() == QStringLiteral("system")
                   ? QStringLiteral("系统音")
                   : QStringLiteral("麦克风")));
  summary_video_->setText(QStringLiteral("H.264 · 默认关闭"));
  summary_policy_->setText(talk_mode_->currentText());
}

}  // namespace talk::desktop
