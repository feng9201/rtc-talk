#include "test.h"
#include "rtc_client/domain/microphone_gate.h"
#include "rtc_client/domain/session.h"

using namespace rtc_client;
using E = SessionEvent;
using S = SessionState;

static void join(SessionLifecycle& s) {
  CHECK(s.apply(E::connect));
  CHECK(s.state() == S::connecting);
  const auto transport = s.stamp();
  CHECK(s.complete(transport, E::transport_open));
  CHECK(s.state() == S::authenticating);
  CHECK(!s.complete(transport, E::hello_new));
  CHECK(s.complete(s.stamp(), E::hello_new));
  CHECK(s.state() == S::ready);
  CHECK(s.apply(E::join));
  CHECK(s.complete(s.stamp(), E::join_ok));
  CHECK(s.state() == S::joined);
  CHECK(s.has_room());
}

int main() {
  return test_main([] {
    MicrophoneGate gate;
    CHECK(!gate.is_open());
    CHECK(!gate.open_if_current(0));
    const auto requested = gate.begin_open_request();
    CHECK(!gate.is_open());
    CHECK(gate.open_if_current(requested));
    CHECK(gate.is_open());
    CHECK(!gate.open_if_current(requested));
    gate.close();
    CHECK(!gate.is_open());
    CHECK(!gate.open_if_current(requested));

    // Mute while an unmute RPC is pending. A late success MUST NOT reopen.
    const auto late = gate.begin_open_request();
    gate.close();
    CHECK(!gate.open_if_current(late));
    const auto next = gate.begin_open_request();
    CHECK(!gate.open_if_current(late));
    CHECK(gate.open_if_current(next));
    gate.close();
    gate.close();
    CHECK(!gate.is_open());

    SessionLifecycle s;
    CHECK(s.state() == S::idle);
    CHECK(!s.has_room());
    CHECK(!s.apply(E::join));
    CHECK(!s.apply(E::transport_open));
    join(s);
    const auto old = s.stamp();
    CHECK(!s.apply(E::connect));
    CHECK(!s.apply(E::join));
    CHECK(s.apply(E::network_lost));
    CHECK(s.state() == S::recovering);
    CHECK(s.stamp().generation > old.generation);
    CHECK(!s.complete(old, E::leave));
    CHECK(!s.apply(E::network_lost));
    CHECK(s.apply(E::transport_open));
    CHECK(s.apply(E::hello_resumed));
    CHECK(s.state() == S::joined);
    CHECK(s.has_room());

    // Recovery window expiry cannot silently rejoin a new session.
    CHECK(s.apply(E::network_lost));
    CHECK(s.apply(E::transport_open));
    CHECK(s.apply(E::hello_new));
    CHECK(s.state() == S::ready);
    CHECK(!s.has_room());
    CHECK(s.apply(E::join));
    auto attempt = s.stamp();
    CHECK(s.apply(E::join_failed));
    CHECK(s.apply(E::join));
    CHECK(!s.complete(attempt, E::join_ok));
    CHECK(s.complete(s.stamp(), E::join_ok));

    CHECK(s.apply(E::leave));
    CHECK(s.apply(E::network_lost));
    CHECK(!s.has_room());
    CHECK(s.apply(E::transport_open));
    CHECK(s.apply(E::hello_resumed));
    CHECK(s.state() == S::ready);

    CHECK(s.apply(E::network_lost));
    attempt = s.stamp();
    CHECK(s.apply(E::resume_expired));
    CHECK(s.state() == S::failed);
    CHECK(!s.has_room());
    CHECK(!s.complete(attempt, E::transport_open));
    CHECK(s.apply(E::connect));
    CHECK(s.apply(E::terminal_failure));
    CHECK(!s.apply(E::join));
    CHECK(s.apply(E::disconnect));
    CHECK(s.state() == S::idle);
    CHECK(s.apply(E::stop));
    CHECK(!s.apply(E::connect));
    CHECK(!s.apply(E::terminal_failure));
    CHECK(s.apply(E::stopped));
    CHECK(!s.apply(E::connect));
    CHECK(!s.apply(E::stop));

    SessionLifecycle initially_resumed;
    CHECK(initially_resumed.apply(E::connect));
    CHECK(initially_resumed.apply(E::transport_open));
    CHECK(!initially_resumed.apply(E::hello_resumed));
    CHECK(initially_resumed.apply(E::hello_new));

    // Exhaustive event sequences: unsupported events must leave state/stamps
    // untouched, and old completions must never be accepted after a transition.
    constexpr E events[] = {
      E::connect, E::transport_open, E::hello_new, E::hello_resumed,
      E::join, E::join_ok, E::join_failed, E::leave, E::leave_ok,
      E::network_lost, E::resume_expired, E::terminal_failure,
      E::disconnect, E::stop, E::stopped
    };
    for (auto a : events) for (auto b : events) for (auto c : events) {
      SessionLifecycle m;
      for (auto e : {a, b, c}) {
        auto before = m.stamp();
        auto state = m.state();
        auto room = m.has_room();
        auto result = m.apply(e);
        if (!result) {
          CHECK(m.stamp() == before);
          CHECK(m.state() == state);
          CHECK(m.has_room() == room);
        } else {
          CHECK(result->revision > before.revision);
          CHECK(!m.complete(before, E::stop));
        }
      }
    }
  });
}
