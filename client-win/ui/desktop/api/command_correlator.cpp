#include "api/command_correlator.h"

#include <QTimer>

namespace talk::desktop {

CommandCorrelator::CommandCorrelator(QObject* parent) : QObject(parent) {
  qRegisterMetaType<CommandResult>();
}

void CommandCorrelator::Track(quint64 command_id, const QString& method,
                              int timeout_ms) {
  if (command_id == 0) return;
  const quint64 token = next_token_++;
  pending_.insert(command_id, Pending{method, token});
  if (timeout_ms <= 0) return;
  QTimer::singleShot(timeout_ms, this, [this, command_id, token] {
    const auto it = pending_.find(command_id);
    if (it == pending_.end() || it->token != token) return;
    const QString method = it->method;
    pending_.erase(it);
    emit CommandTimedOut(command_id, method);
  });
}

bool CommandCorrelator::Resolve(const ResultEvent& result) {
  const auto it = pending_.find(result.command_id);
  if (it == pending_.end()) {
    emit UnmatchedResult(result);
    return false;
  }
  const QString method = it->method;
  pending_.erase(it);
  const CommandResult completed{
      result.command_id, method, result.ok, result.error_code,
      result.error_message, result.data};
  emit CommandCompleted(completed);
  return true;
}

void CommandCorrelator::CancelAll() {
  pending_.clear();
}

}  // namespace talk::desktop
