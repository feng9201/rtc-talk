#pragma once

#include "api/core_event.h"

#include <QHash>
#include <QObject>
#include <QString>

namespace talk::desktop {

class CommandCorrelator final : public QObject {
  Q_OBJECT

 public:
  explicit CommandCorrelator(QObject* parent = nullptr);

  void Track(quint64 command_id, const QString& method,
             int timeout_ms = 15000);
  bool Resolve(const ResultEvent& result);
  void CancelAll();
  int pendingCount() const { return pending_.size(); }

 signals:
  void CommandCompleted(const CommandResult& result);
  void CommandTimedOut(quint64 command_id, const QString& method);
  void UnmatchedResult(const ResultEvent& result);

 private:
  struct Pending {
    QString method;
    quint64 token = 0;
  };

  QHash<quint64, Pending> pending_;
  quint64 next_token_ = 1;
};

}  // namespace talk::desktop
