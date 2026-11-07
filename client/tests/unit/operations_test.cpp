#include "test.h"
#include "rtc_client/domain/operations.h"

#include <set>

using namespace rtc_client;
using namespace std::chrono_literals;

int main() {
  return test_main([] {
    const Operations::Time now{};
    Operations ops(2);
    CHECK(!ops.begin("", now, now + 10s));
    CHECK(!ops.begin(std::string(129, 'x'), now, now + 10s));
    CHECK(!ops.begin("room.join", now, now));
    CHECK(!ops.begin("room.join", now, now - 1ms));
    auto join = ops.begin("room.join", now, now + 10s);
    auto hello = ops.begin("session.hello", now, now + 15s);
    CHECK(join && hello && join != hello);
    CHECK(ops.size() == 2);
    CHECK(!ops.begin("session.ping", now, now + 10s));
    CHECK(!ops.finish(*join, 0, now, OperationCode::ok));
    auto done = ops.finish(*join, 1, now, OperationCode::ok);
    CHECK(done && done->method == "room.join" && done->code == OperationCode::ok);
    CHECK(!ops.finish(*join, 1, now, OperationCode::ok));
    CHECK(ops.expire(now + 14s).empty());
    auto expired = ops.expire(now + 15s);
    CHECK(expired.size() == 1 && expired[0].id == *hello);
    CHECK(expired[0].code == OperationCode::timeout);
    CHECK(!ops.finish(*hello, 1, now, OperationCode::ok));
    CHECK(ops.size() == 0);

    auto old = ops.begin("sfu.publish", now, now + 15s);
    CHECK(old);
    CHECK(ops.advance(1, OperationCode::transport_failed).empty());
    CHECK(ops.size() == 1);
    auto cancelled = ops.advance(2, OperationCode::transport_failed);
    CHECK(cancelled.size() == 1 && cancelled[0].id == *old);
    CHECK(cancelled[0].generation == 1 && cancelled[0].code == OperationCode::transport_failed);
    CHECK(ops.generation() == 2);
    CHECK(!ops.finish(*old, 1, now, OperationCode::ok));
    auto fresh = ops.begin("room.join", now, now + 10s);
    CHECK(fresh && fresh > old);
    CHECK(!ops.finish(*fresh, 1, now, OperationCode::ok));
    CHECK(ops.shutdown().size() == 1);
    CHECK(!ops.begin("session.ping", now, now + 10s));
    CHECK(ops.shutdown().empty());
    Operations zero(0);
    CHECK(!zero.begin("session.ping", now, now + 1s));

    // Exactly one terminal outcome per accepted operation across 100 cycles.
    Operations many(10);
    std::set<std::uint64_t> ids, completed;
    for (unsigned gen = 1; gen <= 100; ++gen) {
      auto drained = many.advance(gen, OperationCode::cancelled);
      for (const auto& result : drained) CHECK(completed.insert(result.id).second);
      for (int n = 0; n < 10; ++n) {
        auto id = many.begin("command", now, now + 1s);
        CHECK(id && ids.insert(*id).second);
        if (n % 2 == 0) {
          auto result = many.finish(*id, gen, now, OperationCode::ok);
          CHECK(result && completed.insert(result->id).second);
        }
      }
    }
    for (const auto& result : many.shutdown()) CHECK(completed.insert(result.id).second);
    CHECK(ids.size() == 1000 && ids == completed);

    // Response dispatch before timer dispatch is still timeout at the deadline.
    Operations deadlines(1);
    auto deadline_id = deadlines.begin("request", now, now + 1s);
    CHECK(deadline_id);
    auto late_response = deadlines.finish(*deadline_id, 1, now + 1s, OperationCode::ok);
    CHECK(late_response && late_response->code == OperationCode::timeout);
    CHECK(deadlines.expire(now + 2s).empty());

    RecoveryWindow recovery(now + 60s);
    CHECK(recovery.next_delay(now, 0) == 1000ms);
    CHECK(recovery.next_delay(now, 250) == 2250ms);
    CHECK(recovery.next_delay(now, 0) == 4000ms);
    CHECK(recovery.next_delay(now, 0) == 8000ms);
    CHECK(recovery.next_delay(now, 250) == 15000ms);
    CHECK(recovery.next_delay(now, 0) == 15000ms);
    CHECK(!recovery.next_delay(now + 59s, 0));
    CHECK(!recovery.next_delay(now + 60s, 0));
    CHECK(!recovery.next_delay(now + 61s, 0));
    recovery.cancel();
    CHECK(!recovery.next_delay(now, 0));
    RecoveryWindow short_ttl(now + 500ms);
    CHECK(!short_ttl.next_delay(now, 0));
  });
}
