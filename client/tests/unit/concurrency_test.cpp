#include "test.h"
#include "rtc_client/domain/microphone_gate.h"
#include "rtc_client/video/budget.h"
#include "rtc_client/video/latest_frame.h"

#include <atomic>
#include <thread>
#include <vector>

using namespace rtc_client;

int main() {
  return test_main([] {
    LatestFrameMailbox<int> mailbox(1);
    std::atomic<int> destroyed{0};
    std::atomic<bool> running{true};
    std::thread producer([&] {
      for (int n = 0; n < 20000; ++n) {
        auto p = std::shared_ptr<const int>(new int(n), [&](const int* value) {
          ++destroyed;
          delete value;
        });
        (void)mailbox.offer(1, std::move(p));
      }
      running = false;
    });
    std::thread consumer([&] {
      while (running.load()) (void)mailbox.take();
    });
    std::thread closer([&] {
      for (int n = 0; n < 200; ++n) std::this_thread::yield();
      mailbox.close();
    });
    producer.join();
    consumer.join();
    closer.join();
    mailbox.close();
    CHECK(destroyed == 20000);

    MicrophoneGate gate;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
      threads.emplace_back([&] {
        for (int n = 0; n < 20000; ++n) {
          const auto ticket = gate.begin_open_request();
          (void)gate.open_if_current(ticket);
          gate.close();
          // This thread has invalidated its own ticket, regardless of peers.
          if (gate.open_if_current(ticket)) std::terminate();
          (void)gate.is_open();
        }
      });
    }
    for (auto& thread : threads) thread.join();
    gate.close();
    CHECK(!gate.is_open());

    VideoLimits limits;
    limits.streams = 4;
    limits.pixels_per_second *= 4;
    limits.bytes *= 4;
    VideoBudget budget(limits);
    threads.clear();
    std::atomic<int> admitted{0};
    std::atomic<bool> exceeded{false};
    for (int t = 0; t < 8; ++t) {
      threads.emplace_back([&, t] {
        for (int n = 0; n < 2000; ++n) {
          auto lease = budget.admit(std::to_string(t), {1920, 1080, 30, 32 * 1024 * 1024});
          if (lease) ++admitted;
          const auto usage = budget.usage();
          if (usage.streams > 4 || usage.pixels_per_second > limits.pixels_per_second ||
              usage.bytes > limits.bytes) exceeded = true;
        }
      });
    }
    for (auto& thread : threads) thread.join();
    CHECK(admitted > 0);
    CHECK(!exceeded);
    CHECK(budget.usage().streams == 0);
    CHECK(budget.usage().bytes == 0);
  });
}
