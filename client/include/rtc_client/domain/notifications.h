#pragma once
#include "rtc_client/domain/session.h"
#include <string_view>

namespace rtc_client {
// Server RoomManager emits room.closed(empty) to the last departing member
// BEFORE the room.leave response. This is not a kick or authorization failure.
inline bool expected_room_closed(SessionState state, std::string_view reason) noexcept {
  return state == SessionState::leaving && reason == "empty";
}
}  // namespace rtc_client
