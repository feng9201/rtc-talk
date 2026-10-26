#include "dialogs/room_policy_dialog.h"

#include "theme/theme.h"
#include "widgets/icon_button.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace talk::desktop {

RoomPolicyDialog::RoomPolicyDialog(QWidget* parent) : QDialog(parent) {
  setObjectName(QStringLiteral("RoomPolicyDialog"));
  setWindowTitle(QStringLiteral("房间策略"));
  setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
  setModal(true);
  setMinimumWidth(460);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(22, 20, 22, 20);
  layout->setSpacing(12);

  auto* header = new QHBoxLayout;
  header->setContentsMargins(0, 0, 0, 0);
  auto* title = Theme::label(QStringLiteral("房间策略"),
                             QStringLiteral("DialogTitle"), this);
  header->addWidget(title, 1);
  auto* close = new IconButton(Theme::Icon::Close, this);
  close->setTone(QStringLiteral("ghost"));
  close->setDiameter(30);
  header->addWidget(close);
  layout->addLayout(header);

  auto* form = new QFormLayout;
  form->setSpacing(10);

  talk_mode_ = new QComboBox(this);
  talk_mode_->addItem(QStringLiteral("自由发言"), "free");
  talk_mode_->addItem(QStringLiteral("按住说话 PTT"), "ptt");
  talk_mode_->addItem(QStringLiteral("主持人审核"), "moderated");
  form->addRow(QStringLiteral("对讲模式"), talk_mode_);

  max_speakers_ = new QSpinBox(this);
  max_speakers_->setRange(0, 20);
  max_speakers_->setSpecialValueText(QStringLiteral("不限制"));
  form->addRow(QStringLiteral("同时开麦上限"), max_speakers_);

  ptt_hold_seconds_ = new QSpinBox(this);
  ptt_hold_seconds_->setRange(5, 600);
  ptt_hold_seconds_->setSuffix(QStringLiteral(" 秒"));
  form->addRow(QStringLiteral("单次持麦上限"), ptt_hold_seconds_);

  ptt_queue_ = new QCheckBox(QStringLiteral("发言权忙时允许排队"), this);
  form->addRow(QString(), ptt_queue_);

  media_mode_ = new QComboBox(this);
  media_mode_->addItem(QStringLiteral("自动"), "auto");
  media_mode_->addItem(QStringLiteral("仅 SFU"), "sfu");
  media_mode_->addItem(QStringLiteral("强制 P2P"), "p2p");
  form->addRow(QStringLiteral("媒体模式"), media_mode_);

  p2p_relay_ = new QCheckBox(QStringLiteral("P2P 允许 TURN 中继"), this);
  form->addRow(QString(), p2p_relay_);

  max_peers_ = new QSpinBox(this);
  max_peers_->setRange(2, 20);
  form->addRow(QStringLiteral("房间人数上限"), max_peers_);

  video_enabled_ = new QCheckBox(QStringLiteral("允许视频"), this);
  form->addRow(QString(), video_enabled_);

  video_max_publishers_ = new QSpinBox(this);
  video_max_publishers_->setRange(0, 20);
  video_max_publishers_->setSpecialValueText(QStringLiteral("不限制"));
  form->addRow(QStringLiteral("视频发布上限"), video_max_publishers_);

  layout->addLayout(form);
  auto* buttons =
      new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                           this);
  buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("应用"));
  buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(close, &IconButton::clicked, this, &QDialog::reject);
}

void RoomPolicyDialog::SetPolicy(const RoomPolicy& policy) {
  talk_mode_->setCurrentIndex(
      talk_mode_->findData(policy.talkMode == TalkMode::Ptt
                               ? QStringLiteral("ptt")
                               : policy.talkMode == TalkMode::Moderated
                                     ? QStringLiteral("moderated")
                                     : QStringLiteral("free")));
  media_mode_->setCurrentIndex(
      media_mode_->findData(policy.mediaMode == MediaModePreference::Sfu
                                ? QStringLiteral("sfu")
                                : policy.mediaMode == MediaModePreference::P2p
                                      ? QStringLiteral("p2p")
                                      : QStringLiteral("auto")));
  max_speakers_->setValue(policy.maxSpeakers);
  ptt_hold_seconds_->setValue(policy.pttMaxHoldSeconds);
  ptt_queue_->setChecked(policy.pttQueue);
  p2p_relay_->setChecked(policy.p2pAllowRelay);
  max_peers_->setValue(policy.maxPeers);
  video_enabled_->setChecked(policy.videoEnabled);
  video_max_publishers_->setValue(policy.videoMaxPublishers);
}

RoomPolicy RoomPolicyDialog::policy() const {
  RoomPolicy result;
  const QString talk_mode = talk_mode_->currentData().toString();
  result.talkMode = talk_mode == QStringLiteral("ptt")
                        ? TalkMode::Ptt
                        : talk_mode == QStringLiteral("moderated")
                              ? TalkMode::Moderated
                              : TalkMode::Free;
  const QString media_mode = media_mode_->currentData().toString();
  result.mediaMode = media_mode == QStringLiteral("sfu")
                         ? MediaModePreference::Sfu
                         : media_mode == QStringLiteral("p2p")
                               ? MediaModePreference::P2p
                               : MediaModePreference::Auto;
  result.maxSpeakers = max_speakers_->value();
  result.pttMaxHoldSeconds = ptt_hold_seconds_->value();
  result.pttQueue = ptt_queue_->isChecked();
  result.p2pAllowRelay = p2p_relay_->isChecked();
  result.maxPeers = max_peers_->value();
  result.videoEnabled = video_enabled_->isChecked();
  result.videoMaxPublishers = video_max_publishers_->value();
  return result;
}

}  // namespace talk::desktop
