#pragma once

#include "domain/app_types.h"

#include <QFrame>

class QLabel;
class QProgressBar;

namespace talk::desktop {

class Avatar;
class IconButton;

class MemberRow final : public QFrame {
  Q_OBJECT

 public:
  explicit MemberRow(QWidget* parent = nullptr);

  void SetMember(const PeerSnapshot& peer, bool self, const QString& self_role,
                 bool can_manage, TalkMode talk_mode,
                 const FloorSnapshot& floor, bool selected);

 signals:
  void memberSelected(const QString& uid);
  void grantRequested(const QString& uid, bool allow);
  void kickRequested(const QString& uid);

 protected:
  void mousePressEvent(QMouseEvent* event) override;

 private:
  void UpdateStyle();

  Avatar* avatar_ = nullptr;
  QLabel* name_ = nullptr;
  QLabel* detail_ = nullptr;
  QLabel* floor_badge_ = nullptr;
  QProgressBar* level_ = nullptr;
  IconButton* more_ = nullptr;
  QString uid_;
  QString role_;
  bool self_ = false;
  bool can_manage_ = false;
  TalkMode talk_mode_ = TalkMode::Free;
  QString holder_uid_;
  QStringList queue_;
};

}  // namespace talk::desktop
