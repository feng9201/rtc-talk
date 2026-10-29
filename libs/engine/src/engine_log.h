#pragma once

#include <string>

namespace rtcwin::engine {

void InitializeEngineLogging();
void ShutdownEngineLogging();
std::string EngineLogPath();

}  // namespace rtcwin::engine
