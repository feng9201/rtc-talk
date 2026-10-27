#include "rtcwin/ui/pages/prejoin_page.h"

#include "rtcwin/ui/theme/theme.h"
#include "rtcwin/ui/video/video_surface.h"
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
#include <QSlider>
#include <QStyle>
#include <QVBoxLayout>

namespace rtcwin::ui {
namespace {

QString Text(const std::string& value) {
  return QString::fromStdString(value);
}

std::string Value(const QString& value) {
  return value.toStdString();
}

QString ComboValue(const QComboBox* combo, const QString& fallback) {
  if (combo == nullptr || combo->currentIndex() < 0) return fallback;
  return combo->currentData().toString();
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
  auto* content_layout = new QHBoxLayout(content);
  content_layout->setContentsMargins(26, 22, 26, 26);
  content_layout->setSpacing(22);
  content_layout->addWidget(BuildForm(), 62);
  content_layout->addWidget(BuildPreview(), 38);
  scroll->setWidget(content);
  root->addWidget(scroll, 1);
  UpdateSummary();
}

QWidget* PrejoinPage::BuildHeader() {
  auto* header = new QFrame(this);
  header->setObjectName(QStringLiteral("AppHeader"));
  header->setFixedHeight(66);
  auto* layout = new QHBoxLayout(header);
  layout->setContentsMargins(22, 0, 132, 0);
  layout->setSpacing(12);

  auto* brand = new QLabel(QStringLiteral("RT"), header);
  brand->setObjectName(QStringLiteral("BrandMark"));
  brand->setAlignment(Qt::AlignCenter);
  brand->setFixedSize(42, 42);
  layout->addWidget(brand);

  auto* titles = new QVBoxLayout;
  titles->setSpacing(1);
  titles->addWidget(theme::Label(QStringLiteral("RTC Talk"),
                                 QStringLiteral("AppName"), header));
  titles->addWidget(theme::Label(QStringLiteral("生产桌面客户端 · Windows"),
                                 QStringLiteral("AppSubtitle"), header));
  layout->addLayout(titles);
  layout->addStretch(1);
  layout->addWidget(new Pill(QStringLiteral("1 连接配置"),
                             QStringLiteral("accent"), header));
  layout->addWidget(new Pill(QStringLiteral("2 房间"),
                             QStringLiteral("default"), header));
  return header;
}

QWidget* PrejoinPage::BuildForm() {
  auto* card = theme::Card(this);
  auto* layout = new QVBoxLayout(card);
  layout->setContentsMargins(22, 20, 22, 20);
  layout->setSpacing(13);

  auto* title_row = new QHBoxLayout;
  auto* titles = new QVBoxLayout;
  titles->setSpacing(2);
  titles->addWidget(theme::Label(QStringLiteral("连接与入房"),
                                 QStringLiteral("Title"), card));
  titles->addWidget(theme::Label(QStringLiteral("建立真实信令、设备与房间会话"),
                                 QStringLiteral("Subtitle"), card));
  title_row->addLayout(titles, 1);
  title_row->addWidget(new Pill(QStringLiteral("PRODUCTION"),
                                QStringLiteral("accent"), card),
                       0, Qt::AlignTop);
  layout->addLayout(title_row);

  url_ = new QLineEdit(card);
  url_->setPlaceholderText(QStringLiteral("wss://server/talk/v1/ws"));
  user_id_ = new QLineEdit(card);
  user_id_->setPlaceholderText(QStringLiteral("用户标识"));
  room_id_ = new QLineEdit(card);
  room_id_->setPlaceholderText(QStringLiteral("房间 ID"));
  token_ = new QLineEdit(card);
  token_->setEchoMode(QLineEdit::Password);
  token_->setPlaceholderText(QStringLiteral("仅保存在内存，不写入配置"));
  dev_token_ = new QCheckBox(QStringLiteral("开发测试：从服务端获取临时凭证"),
                             card);

  layout->addWidget(MakeField(QStringLiteral("信令服务"), url_));
  layout->addWidget(MakePair(MakeField(QStringLiteral("用户标识"), user_id_),
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
  layout->addWidget(theme::Label(QStringLiteral("音频源"),
                                 QStringLiteral("SectionTitle"), card));
  layout->addWidget(audio_source_);

  recording_device_ = new QComboBox(card);
  recording_device_->addItem(QStringLiteral("媒体引擎接入后枚举"), QString());
  recording_device_->setEnabled(false);
  playout_device_ = new QComboBox(card);
  playout_device_->addItem(QStringLiteral("媒体引擎接入后枚举"), QString());
  playout_device_->setEnabled(false);
  camera_device_ = new QComboBox(card);
  camera_device_->addItem(QStringLiteral("媒体引擎接入后枚举"), QString());
  camera_device_->setEnabled(false);
  layout->addWidget(MakePair(
      MakeField(QStringLiteral("麦克风设备"), recording_device_),
      MakeField(QStringLiteral("播放设备"), playout_device_)));
  layout->addWidget(MakeField(QStringLiteral("摄像头"), camera_device_));

  auto* volume_row = new QHBoxLayout;
  volume_ = new QSlider(Qt::Horizontal, card);
  volume_->setRange(0, 100);
  volume_->setValue(60);
  volume_value_ = theme::Label(QStringLiteral("60%"),
                               QStringLiteral("Value"), card);
  volume_value_->setFixedWidth(44);
  volume_row->addWidget(volume_, 1);
  volume_row->addWidget(volume_value_);
  layout->addWidget(theme::Label(QStringLiteral("播放音量"),
                                 QStringLiteral("SectionTitle"), card));
  layout->addLayout(volume_row);

  auto* actions = new QHBoxLayout;
  actions->setContentsMargins(0, 6, 0, 0);
  save_button_ = theme::Button(QStringLiteral("保存设置"),
                               QStringLiteral("default"), card);
  join_button_ = theme::Button(QStringLiteral("进入房间"),
                               QStringLiteral("primary"), card);
  actions->addWidget(save_button_);
  actions->addStretch(1);
  actions->addWidget(join_button_);
  layout->addLayout(actions);

  connect(volume_, &QSlider::valueChanged, this, [this](int value) {
    volume_value_->setText(QStringLiteral("%1%").arg(value));
  });
  connect(join_button_, &QPushButton::clicked, this,
          [this] { emit JoinRequested(Options()); });
  connect(save_button_, &QPushButton::clicked, this,
          [this] { emit SaveRequested(Options()); });
  connect(talk_mode_,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this] { UpdateSummary(); });
  return card;
}

QWidget* PrejoinPage::BuildPreview() {
  auto* column = new QWidget(this);
  column->setMinimumWidth(360);
  auto* layout = new QVBoxLayout(column);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(16);

  auto* card = theme::Card(column);
  auto* card_layout = new QVBoxLayout(card);
  card_layout->setContentsMargins(14, 14, 14, 14);
  card_layout->setSpacing(12);

  preview_surface_ = new ImageVideoSurface(card);
  preview_surface_->SetPlaceholder(QStringLiteral("本地预览已关闭"));
  preview_surface_->setMinimumHeight(320);
  card_layout->addWidget(preview_surface_, 1);

  auto* identity = new QHBoxLayout;
  auto* identity_text = new QVBoxLayout;
  identity_text->setSpacing(2);
  identity_text->addWidget(theme::Label(QStringLiteral("本机视频"),
                                        QStringLiteral("TitleSmall"), card));
  preview_status_ = theme::Label(QStringLiteral("等待设备枚举"),
                                 QStringLiteral("Hint"), card);
  identity_text->addWidget(preview_status_);
  identity->addLayout(identity_text, 1);
  preview_button_ = theme::Button(QStringLiteral("启动本地预览"),
                                  QStringLiteral("default"), card);
  identity->addWidget(preview_button_);
  card_layout->addLayout(identity);
  layout->addWidget(card, 1);

  auto* summary = theme::Card(column);
  auto* summary_layout = new QVBoxLayout(summary);
  summary_layout->setContentsMargins(16, 14, 16, 14);
  summary_layout->setSpacing(9);
  summary_layout->addWidget(theme::Label(QStringLiteral("本次入房"),
                                         QStringLiteral("SectionTitle"),
                                         summary));
  const auto add_row = [summary, summary_layout](const QString& name) {
    auto* row = new QHBoxLayout;
    row->addWidget(theme::Label(name, QStringLiteral("Muted"), summary), 1);
    auto* value =
        theme::Label(QString(), QStringLiteral("SummaryValue"), summary);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(value);
    summary_layout->addLayout(row);
    return value;
  };
  summary_video_ = add_row(QStringLiteral("视频"));
  summary_policy_ = add_row(QStringLiteral("房间模式"));
  status_ = theme::Label(QStringLiteral("生产骨架已就绪"),
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
  layout->addWidget(theme::Label(label, QStringLiteral("FieldLabel"), field));
  layout->addWidget(editor);
  if (!hint.isEmpty()) {
    layout->addWidget(theme::Label(hint, QStringLiteral("Hint"), field));
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

  const QString backend =
      ComboValue(audio_backend_, QStringLiteral("wasapi"));
  result.audio_backend = backend == QStringLiteral("none")
                             ? domain::AudioBackend::None
                             : backend == QStringLiteral("fake")
                                   ? domain::AudioBackend::Fake
                                   : domain::AudioBackend::Wasapi;
  const QString mode = ComboValue(media_mode_, QStringLiteral("auto"));
  result.media_mode = mode == QStringLiteral("p2p")
                          ? domain::MediaModePreference::P2p
                          : mode == QStringLiteral("sfu")
                                ? domain::MediaModePreference::Sfu
                                : domain::MediaModePreference::Auto;
  const QString talk = ComboValue(talk_mode_, QStringLiteral("free"));
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
  result.recording_device = Value(recording_device_->currentData().toString());
  result.playout_device = Value(playout_device_->currentData().toString());
  result.camera_device = Value(camera_device_->currentData().toString());
  return result;
}

void PrejoinPage::SetOptions(const domain::PrejoinOptions& options) {
  url_->setText(Text(options.signaling_url));
  user_id_->setText(Text(options.user_id));
  room_id_->setText(Text(options.room_id));
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
  UpdateSummary();
}

void PrejoinPage::SetBusy(bool busy) {
  join_button_->setEnabled(!busy);
  save_button_->setEnabled(!busy);
  preview_button_->setEnabled(!busy);
}

void PrejoinPage::SetStatus(const QString& text, const QString& tone) {
  status_->setText(text);
  status_->setProperty("tone", tone);
  status_->style()->unpolish(status_);
  status_->style()->polish(status_);
}

void PrejoinPage::TogglePreview() {
  preview_active_ = !preview_active_;
  preview_button_->setText(preview_active_
                               ? QStringLiteral("关闭本地预览")
                               : QStringLiteral("启动本地预览"));
  preview_surface_->SetPlaceholder(
      preview_active_ ? QStringLiteral("等待 muxPlayer 本地预览帧")
                      : QStringLiteral("本地预览已关闭"));
  preview_surface_->Clear();
  preview_status_->setText(
      preview_active_ ? QStringLiteral("设备/Engine 待接入")
                      : QStringLiteral("等待设备枚举"));
  emit PreviewToggleRequested();
}

void PrejoinPage::UpdateSummary() {
  if (summary_video_ == nullptr) return;
  summary_video_->setText(QStringLiteral("H.264 · muxPlayer 渲染"));
  summary_policy_->setText(talk_mode_->currentText());
}

}  // namespace rtcwin::ui
