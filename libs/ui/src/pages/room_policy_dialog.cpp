#include "rtcwin/ui/pages/room_policy_dialog.h"

#include "rtcwin/ui/theme/theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace rtcwin::ui {

RoomPolicyDialog::RoomPolicyDialog(const domain::RoomPolicy& policy,
                                   QWidget* parent)
    : QDialog(parent) {
  setObjectName(QStringLiteral("RoomPolicyDialog"));
  setWindowTitle(QStringLiteral("房间策略"));
  setModal(true);
  setMinimumWidth(420);
  BuildUi(policy);
}

void RoomPolicyDialog::BuildUi(const domain::RoomPolicy& policy) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(20, 18, 20, 18);
  root->setSpacing(14);

  root->addWidget(theme::Label(QStringLiteral("房间策略"),
                               QStringLiteral("TitleMedium"), this));
  auto* form = new QFormLayout;
  form->setLabelAlignment(Qt::AlignLeft);
  form->setFormAlignment(Qt::AlignTop);
  form->setVerticalSpacing(10);

  talk_mode_ = new QComboBox(this);
  talk_mode_->addItem(QStringLiteral("自由发言"), QStringLiteral("free"));
  talk_mode_->addItem(QStringLiteral("按住说话 PTT"),
                      QStringLiteral("ptt"));
  talk_mode_->addItem(QStringLiteral("主持人审核"),
                      QStringLiteral("moderated"));
  const QString mode = QString::fromStdString(
      domain::TalkModeName(policy.talk_mode));
  const int index = talk_mode_->findData(mode);
  talk_mode_->setCurrentIndex(index < 0 ? 0 : index);

  max_speakers_ = new QSpinBox(this);
  max_speakers_->setRange(0, 50);
  max_speakers_->setValue(policy.max_speakers);

  ptt_hold_seconds_ = new QSpinBox(this);
  ptt_hold_seconds_->setRange(5, 600);
  ptt_hold_seconds_->setValue(policy.ptt_max_hold_seconds);

  ptt_queue_ = new QCheckBox(QStringLiteral("允许排队"), this);
  ptt_queue_->setChecked(policy.ptt_queue);

  media_mode_ = new QComboBox(this);
  media_mode_->addItem(QStringLiteral("自动选择"), QStringLiteral("auto"));
  media_mode_->addItem(QStringLiteral("SFU"), QStringLiteral("sfu"));
  media_mode_->addItem(QStringLiteral("P2P"), QStringLiteral("p2p"));
  const QString media_mode =
      QString::fromStdString(domain::MediaModeName(policy.media_mode));
  const int media_index = media_mode_->findData(media_mode);
  media_mode_->setCurrentIndex(media_index < 0 ? 0 : media_index);

  p2p_allow_relay_ = new QCheckBox(QStringLiteral("允许 P2P 中继"), this);
  p2p_allow_relay_->setChecked(policy.p2p_allow_relay);

  max_peers_ = new QSpinBox(this);
  max_peers_->setRange(2, 50);
  max_peers_->setValue(policy.max_peers);

  video_enabled_ = new QCheckBox(QStringLiteral("允许视频"), this);
  video_enabled_->setChecked(policy.video_enabled);

  video_max_publishers_ = new QSpinBox(this);
  video_max_publishers_->setRange(0, 20);
  video_max_publishers_->setValue(policy.video_max_publishers);

  form->addRow(QStringLiteral("对讲模式"), talk_mode_);
  form->addRow(QStringLiteral("自由发言人数上限"), max_speakers_);
  form->addRow(QStringLiteral("PTT 最长占麦（秒）"), ptt_hold_seconds_);
  form->addRow(QStringLiteral("PTT 队列"), ptt_queue_);
  form->addRow(QStringLiteral("媒体模式"), media_mode_);
  form->addRow(QStringLiteral("P2P 中继"), p2p_allow_relay_);
  form->addRow(QStringLiteral("房间人数上限"), max_peers_);
  form->addRow(QStringLiteral("视频"), video_enabled_);
  form->addRow(QStringLiteral("视频发布者上限"), video_max_publishers_);
  root->addLayout(form);

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("应用"));
  buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
  connect(buttons, &QDialogButtonBox::accepted, this,
          &RoomPolicyDialog::Apply);
  connect(buttons, &QDialogButtonBox::rejected, this,
          &QDialog::reject);
  root->addWidget(buttons);
}

void RoomPolicyDialog::Apply() {
  domain::RoomPolicyPatch patch;
  patch.talk_mode = talk_mode_->currentData().toString() ==
                            QStringLiteral("ptt")
                        ? domain::TalkMode::Ptt
                        : talk_mode_->currentData().toString() ==
                                  QStringLiteral("moderated")
                              ? domain::TalkMode::Moderated
                              : domain::TalkMode::Free;
  patch.max_speakers = max_speakers_->value();
  patch.ptt_max_hold_seconds = ptt_hold_seconds_->value();
  patch.ptt_queue = ptt_queue_->isChecked();
  const QString media_mode = media_mode_->currentData().toString();
  patch.media_mode =
      media_mode == QStringLiteral("sfu")
          ? domain::MediaModePreference::Sfu
          : media_mode == QStringLiteral("p2p")
                ? domain::MediaModePreference::P2p
                : domain::MediaModePreference::Auto;
  patch.p2p_allow_relay = p2p_allow_relay_->isChecked();
  patch.max_peers = max_peers_->value();
  patch.video_enabled = video_enabled_->isChecked();
  patch.video_max_publishers = video_max_publishers_->value();
  emit PolicyChanged(patch);
  accept();
}

}  // namespace rtcwin::ui
