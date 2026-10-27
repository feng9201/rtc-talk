#include "rtcwin/ui/widgets/member_row.h"

#include "rtcwin/ui/theme/theme.h"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QProgressBar>
#include <QStyle>
#include <QVBoxLayout>

namespace rtcwin::ui {

MemberRow::MemberRow(QWidget* parent) : QFrame(parent) {
  setObjectName(QStringLiteral("MemberRow"));
  setCursor(Qt::PointingHandCursor);
  setMinimumHeight(68);

  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(10, 8, 10, 8);
  layout->setSpacing(10);

  avatar_ = new QLabel(this);
  avatar_->setFixedSize(38, 38);
  avatar_->setAlignment(Qt::AlignCenter);
  avatar_->setStyleSheet(
      QStringLiteral("background:#223746;border-radius:19px;"
                     "color:#cae2f1;font-size:16px;font-weight:700;"));
  layout->addWidget(avatar_);

  auto* text = new QVBoxLayout;
  text->setContentsMargins(0, 0, 0, 0);
  text->setSpacing(3);
  auto* title_line = new QHBoxLayout;
  title_line->setContentsMargins(0, 0, 0, 0);
  title_line->setSpacing(6);
  name_ = new QLabel(this);
  name_->setObjectName(QStringLiteral("TitleSmall"));
  badge_ = new QLabel(this);
  badge_->setObjectName(QStringLiteral("Pill"));
  badge_->setProperty("tone", QStringLiteral("accent"));
  badge_->hide();
  title_line->addWidget(name_);
  title_line->addWidget(badge_);
  title_line->addStretch(1);
  text->addLayout(title_line);

  meta_ = new QLabel(this);
  meta_->setObjectName(QStringLiteral("Hint"));
  text->addWidget(meta_);
  level_ = new QProgressBar(this);
  level_->setRange(0, 100);
  level_->setTextVisible(false);
  level_->setFixedHeight(3);
  text->addWidget(level_);
  layout->addLayout(text, 1);
}

void MemberRow::SetMember(const domain::PeerState& member, bool self, bool host,
                          bool selected) {
  uid_ = QString::fromStdString(member.uid);
  role_ = QString::fromStdString(member.display_name);
  avatar_->setText(theme::AvatarText(
      QString::fromStdString(member.display_name)));
  QString name = QString::fromStdString(member.display_name);
  if (self) name += QStringLiteral("（我）");
  name_->setText(name);
  badge_->setText(member.role == domain::RoomRole::Host
                      ? QStringLiteral("主持人")
                      : QString());
  badge_->setVisible(member.role == domain::RoomRole::Host);

  QStringList parts;
  parts << (member.audio_muted ? QStringLiteral("闭麦")
                               : QStringLiteral("麦克风"));
  parts << (member.camera_on ? QStringLiteral("视频开")
                             : QStringLiteral("视频关"));
  if (member.screen_on) parts << QStringLiteral("共享中");
  if (member.speaking) parts << QStringLiteral("发言中");
  meta_->setText(parts.join(QStringLiteral(" · ")));
  level_->setValue(
      qBound(0, qRound(member.audio_level * 100.0), 100));
  setProperty("selected", selected);
  style()->unpolish(this);
  style()->polish(this);
  Q_UNUSED(host);
}

void MemberRow::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) emit Selected(uid_);
  QFrame::mousePressEvent(event);
}

void MemberRow::contextMenuEvent(QContextMenuEvent* event) {
  QMenu menu(this);
  auto* grant = menu.addAction(QStringLiteral("授予发言权"));
  auto* revoke = menu.addAction(QStringLiteral("收回发言权"));
  menu.addSeparator();
  auto* kick = menu.addAction(QStringLiteral("移出房间"));
  QAction* selected = menu.exec(event->globalPos());
  if (selected == grant) emit GrantRequested(uid_, true);
  if (selected == revoke) emit GrantRequested(uid_, false);
  if (selected == kick) emit KickRequested(uid_);
}

}  // namespace rtcwin::ui
