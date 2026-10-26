#pragma once

#include "api/core_event.h"

#include <QJsonObject>
#include <QString>

namespace talk::desktop {

class EventMapper final {
 public:
  static CoreEvent Map(const QString& name, const QJsonObject& data);
};

}  // namespace talk::desktop
