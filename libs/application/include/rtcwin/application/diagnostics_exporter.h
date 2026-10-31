#pragma once

#include "rtcwin/domain/types.h"

#include <QString>

namespace rtcwin::application {

class DiagnosticsExporter final {
 public:
  static bool Export(const domain::SessionSnapshot& snapshot,
                     const QString& log_path,
                     const QString& output_directory,
                     QString* output_zip,
                     QString* error);
};

}  // namespace rtcwin::application
