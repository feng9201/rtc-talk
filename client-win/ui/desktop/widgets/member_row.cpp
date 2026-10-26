#include "widgets/member_row.h"

#include "theme/theme.h"
#include "widgets/avatar.h"
#include "widgets/icon_button.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QProgressBar>
#include <QStyle>
#include <QVBoxLayout>

namespace talk::desktop {

MemberRow::MemberRow(QWidget* parent) : QFrame(parent) {
  setObjectName(QStringLiteral("MemberRow"));
  setCursor(Qt::PointingHandCursor);
  setProperty("noWindowDrag", true);

  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(10, 8, 8, 8);
  layout->setSpacing(9);
  avatar_ = new Avatar(this);
  avatar_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  layout->addWidget(avatar_);

  auto* identity = new QVBoxLayout;
  identity->setContentsMargins(0, 0, 0, 0);
  identity->setSpacing(2);
  name_ = Theme::label(QString(), QStringLiteral("TitleSmall"), this);
  name_->setMinimumWidth(0);
  name_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  detail_ = Theme::label(QString(), QStringLiteral("Hint"), this);
  detail_->setWordWrap(false);
  detail_->setMinimumWidth(0);
  detail_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  identity->addWidget(name_);
  identity->addWidget(detail_);
  layout->addLayout(identity, 1);

  floor_badge_ = Theme::label(QStringLiteral("发言"),
                              QStringLiteral("MemberFloorBadge"), this);
  floor_badge_->hide();
  layout->addWidget(floor_badge_);

  level_ = new QProgressBar(this);
  level_->setObjectName(QStringLiteral("MemberLevel"));
  level_->setRange(0, 100);
  level_->setValue(0);
  level_->setTextVisible(false);
  level_->setFixedSize(38, 5);
  layout->addWidget(level_);

  more_ = new IconButton(Theme::Icon::More, this);
  more_->setTone(QStringLiteral("ghost"));
  more_->setDiameter(26);
  more_->setToolTip(QStringLiteral("成员操作"));
  layout->addWidget(more_);

  connect(more_, &IconButton::clicked, this, [this] {
    if (!can_manage_ || uid_.isEmpty() || self_) return;
    QMenu menu(this);
    QAction* grant = nullptr;
    if (talk_mode_ != TalkMode::Free) {
      grant = menu.addAction(holder_uid_ == uid_ ? QStringLiteral("收回发言权")
                                                 : QStringLiteral("授予发言权"));
    }
    QAction* kick = menu.addAction(QStringLiteral("移出房间"));
    QAction* chosen =
        menu.exec(more_->mapToGlobal(QPoint(0, more_->height())));
    if (grant != nullptr && chosen == grant) {
      emit grantRequested(uid_, holder_uid_ != uid_);
    } else if (chosen == kick) {
      emit kickRequested(uid_);
    }
  });
}

void MemberRow::SetMember(const PeerSnapshot& peer, bool self,
                          const QString& self_role, bool can_manage,
                          TalkMode talk_mode, const FloorSnapshot& floor,
                          bool selected) {
  uid_ = peer.uid;
  role_ = self ? self_role : peer.role;
  self_ = self;
  can_manage_ = can_manage;
  talk_mode_ = talk_mode;
  holder_uid_ = floor.holderUid;
  queue_ = floor.queue;

  avatar_->SetText(peer.displayName.isEmpty() ? peer.uid : peer.displayName);
  QString name = peer.displayName.isEmpty() ? peer.uid : peer.displayName;
  if (self) name += QStringLiteral("（我）");
  name_->setText(name);

  QStringList detail;
  detail << (role_ == QStringLiteral("host") ? QStringLiteral("主持人")
                                              : QStringLiteral("成员"));
  detail << (peer.audioMuted ? QStringLiteral("闭麦") : QStringLiteral("开麦"));
  detail << (peer.cameraOn ? QStringLiteral("视频开")
                           : QStringLiteral("视频关"));
  if (peer.screenOn) detail << QStringLiteral("共享中");
  const int queue_index = queue_.indexOf(peer.uid);
  if (queue_index >= 0) {
    detail << QStringLiteral("排队 %1").arg(queue_index + 1);
  }
  detail_->setText(detail.join(QStringLiteral(" · ")));

  const bool is_holder = !holder_uid_.isEmpty() && holder_uid_ == peer.uid;
  floor_badge_->setVisible(is_holder);
  floor_badge_->setText(self ? QStringLiteral("我在发言")
                             : QStringLiteral("发言中"));
  level_->setValue(qBound(0, qRound(peer.audioLevel * 100.0), 100));
  setProperty("selected", selected);
  setProperty("speaking", peer.speaking || is_holder);
  more_->setVisible(can_manage && !self);
  UpdateStyle();
}

void MemberRow::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && !uid_.isEmpty()) {
    emit memberSelected(uid_);
  }
  QFrame::mousePressEvent(event);
}

void MemberRow::UpdateStyle() {
  style()->unpolish(this);
  style()->polish(this);
  update();
}

}  // namespace talk::desktop
