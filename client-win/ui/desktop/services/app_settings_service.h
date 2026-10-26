#pragma once

#include "domain/app_types.h"

#include <QString>

namespace talk::desktop {

class AppSettingsService final {
 public:
  explicit AppSettingsService(const QString& ini_path = QString());

  PrejoinOptions Load() const;
  void Save(const PrejoinOptions& options) const;

 private:
  QString ini_path_;
};

}  // namespace talk::desktop
