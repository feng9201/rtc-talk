#include "rtcwin/engine/reconnect_controller.h"

#include <iostream>

namespace {

int failures = 0;

void Expect(bool condition, const char* message) {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

}  // namespace

int main() {
  using rtcwin::engine::ReconnectController;
  ReconnectController reconnect;
  const auto start = ReconnectController::Clock::now();
  reconnect.Begin(start, 10);
  Expect(reconnect.active(), "reconnect controller did not activate");

  auto decision = reconnect.Next(start);
  Expect(decision.retry && decision.attempt == 1 &&
             decision.delay_seconds == 1,
         "first retry should wait one second");
  decision = reconnect.Next(start + std::chrono::seconds(1));
  Expect(decision.retry && decision.attempt == 2 &&
             decision.delay_seconds == 2,
         "second retry should wait two seconds");
  decision = reconnect.Next(start + std::chrono::seconds(3));
  Expect(decision.retry && decision.attempt == 3 &&
             decision.delay_seconds == 4,
         "third retry should wait four seconds");
  decision = reconnect.Next(start + std::chrono::seconds(11));
  Expect(!decision.retry && decision.expired,
         "expired reconnect should stop retrying");
  Expect(!reconnect.active(), "expired reconnect remained active");

  reconnect.Begin(start, 10);
  reconnect.Reset();
  Expect(!reconnect.active(), "reset did not clear reconnect state");

  if (failures != 0) {
    std::cerr << failures << " reconnect controller test(s) failed\n";
    return 1;
  }
  std::cout << "reconnect_controller_test: PASS\n";
  return 0;
}
