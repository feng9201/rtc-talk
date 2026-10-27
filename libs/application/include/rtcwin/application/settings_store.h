#pragma once

#include "rtcwin/domain/types.h"

#include <QString>

namespace rtcwin::application {

class SettingsStore {
 public:
  explicit SettingsStore(QString file_path = {});
  ~SettingsStore();

  SettingsStore(const SettingsStore&) = delete;
  SettingsStore& operator=(const SettingsStore&) = delete;

  domain::PrejoinOptions Load() const;
  void Save(const domain::PrejoinOptions& options) const;

 private:
  class Impl;
  Impl* impl_;
};

}  // namespace rtcwin::application
