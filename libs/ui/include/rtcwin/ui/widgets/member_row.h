#pragma once

#include "rtcwin/domain/types.h"

#include <QFrame>

class QLabel;
class QProgressBar;

namespace rtcwin::ui {

class MemberRow final : public QFrame {
  Q_OBJECT

 public:
  explicit MemberRow(QWidget* parent = nullptr);

  void SetMember(const domain::PeerState& member, bool self, bool host,
                 bool selected);
  const QString& uid() const { return uid_; }

 signals:
  void Selected(const QString& uid);
  void GrantRequested(const QString& uid, bool allow);
  void KickRequested(const QString& uid);

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;

 private:
  QString uid_;
  QString role_;
  QLabel* avatar_ = nullptr;
  QLabel* name_ = nullptr;
  QLabel* meta_ = nullptr;
  QLabel* badge_ = nullptr;
  QProgressBar* level_ = nullptr;
};

}  // namespace rtcwin::ui
