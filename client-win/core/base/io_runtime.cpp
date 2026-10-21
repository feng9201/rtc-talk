// File: core/base/io_runtime.cpp
// Brief: 实现核心控制线程的启动与停止。
#include "base/io_runtime.h"

#include <utility>

namespace talk {

IoRuntime::IoRuntime()
    : guard_(asio::make_work_guard(io_)),
      thread_([this] { io_.run(); }) {}

IoRuntime::~IoRuntime() { Stop(); }

void IoRuntime::Stop() {
  if (stopped_) {
    return;
  }
  stopped_ = true;
  guard_.reset();
  io_.stop();
  if (thread_.joinable()) {
    thread_.join();
  }
}

}  // namespace talk
