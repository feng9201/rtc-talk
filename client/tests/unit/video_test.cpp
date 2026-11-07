#include "test.h"
#include "rtc_client/video/budget.h"
#include "rtc_client/video/frame.h"
#include "rtc_client/video/latest_frame.h"

#include <functional>
#include <limits>

using namespace rtc_client;

struct Tracked {
  explicit Tracked(std::function<void()> fn) : on_destroy(std::move(fn)) {}
  ~Tracked() { if (on_destroy) on_destroy(); }
  std::function<void()> on_destroy;
};

static FrameDescription nv12() {
  FrameDescription d;
  d.fd = 0;  // fd zero is valid; never reject it as false.
  d.width = 1920;
  d.height = 1080;
  d.y_stride = d.uv_stride = 1920;
  d.allocated_y_rows = 1088;
  d.uv_offset = 1920 * 1088;
  d.allocation_bytes = 1920 * 1088 * 3 / 2;
  return d;
}

int main() {
  return test_main([] {
    auto d = nv12();
    CHECK(valid_frame_description(d));
    CHECK(!DecodedFrame::create(d, {}));
    auto owner = std::make_shared<int>(1);
    std::weak_ptr<void> weak_owner = owner;
    auto frame = DecodedFrame::create(d, owner);
    CHECK(frame);
    owner.reset();
    CHECK(!weak_owner.expired());
    frame.reset();
    CHECK(weak_owner.expired());
    d.width = 0; CHECK(!valid_frame_description(d));
    d = nv12(); d.width = 1919; CHECK(!valid_frame_description(d));
    d = nv12(); d.fd = -1; CHECK(!valid_frame_description(d));
    d = nv12(); d.y_stride = 1919; CHECK(!valid_frame_description(d));
    d = nv12(); d.uv_stride = 1919; CHECK(!valid_frame_description(d));
    d = nv12(); d.allocated_y_rows = 1078; CHECK(!valid_frame_description(d));
    d = nv12(); d.allocated_y_rows = 1089; CHECK(!valid_frame_description(d));
    d = nv12(); d.allocation_bytes--; CHECK(!valid_frame_description(d));
    d = nv12(); d.uv_offset--; CHECK(!valid_frame_description(d));
    d = nv12(); d.y_offset = std::numeric_limits<std::uint64_t>::max();
    CHECK(!valid_frame_description(d));
    d = nv12(); d.uv_offset = std::numeric_limits<std::uint64_t>::max();
    CHECK(!valid_frame_description(d));
    d = nv12(); d.rotation = 45; CHECK(!valid_frame_description(d));
    d = nv12(); d.rotation = 90; CHECK(valid_frame_description(d));
    d = nv12(); d.modifier = 1; CHECK(!valid_frame_description(d));
    d = nv12(); d.format = PixelFormat::unsupported; CHECK(!valid_frame_description(d));

    LatestFrameMailbox<Tracked> mailbox(1);
    int reclaimed = 0;
    auto tracked = [&] {
      return std::make_shared<Tracked>([&] { ++reclaimed; (void)mailbox.take(); });
    };
    // Each custom destructor re-enters the queue: this hangs if released under lock.
    CHECK(mailbox.offer(1, tracked()) == FrameOffer::accepted);
    CHECK(mailbox.offer(0, tracked()) == FrameOffer::stale);
    // Stale frame's destructor took the previously pending frame out.
    CHECK(reclaimed == 2);
    CHECK(!mailbox.take());
    CHECK(mailbox.offer(1, std::make_shared<Tracked>([&] { ++reclaimed; })) == FrameOffer::accepted);
    CHECK(mailbox.offer(1, std::make_shared<Tracked>([&] { ++reclaimed; })) == FrameOffer::replaced);
    CHECK(reclaimed == 3);
    auto kept = mailbox.take();
    CHECK(kept);
    CHECK(!mailbox.take());
    kept.reset();
    CHECK(reclaimed == 4);
    CHECK(mailbox.offer(1, tracked()) == FrameOffer::accepted);
    CHECK(!mailbox.advance(1));
    CHECK(mailbox.advance(2));
    CHECK(reclaimed == 5);
    CHECK(mailbox.offer(1, tracked()) == FrameOffer::stale);
    CHECK(mailbox.offer(2, {}) == FrameOffer::invalid);
    CHECK(mailbox.offer(2, tracked()) == FrameOffer::accepted);
    mailbox.close();
    CHECK(reclaimed == 7);
    CHECK(mailbox.offer(2, tracked()) == FrameOffer::closed);
    CHECK(!mailbox.advance(3));

    InFlightFrames<Tracked> in_flight(2);
    int releases = 0;
    auto lease = [&] {
      return std::make_shared<Tracked>([&] { ++releases; (void)in_flight.size(); });
    };
    CHECK(in_flight.commit(1, lease()));
    CHECK(!in_flight.commit(1, lease()));
    CHECK(releases == 1);  // Only the rejected duplicate is reclaimed.
    CHECK(in_flight.size() == 1);
    CHECK(in_flight.commit(2, lease()));
    CHECK(!in_flight.commit(3, lease()));
    CHECK(releases == 2);
    CHECK(in_flight.size() == 2);  // Overflow never evicts a committed buffer.
    CHECK(in_flight.release(1));
    CHECK(!in_flight.release(1));
    CHECK(releases == 3);
    in_flight.disconnect_confirmed();
    CHECK(releases == 4);
    CHECK(in_flight.size() == 0);
    CHECK(!in_flight.commit(4, lease()));
    CHECK(!in_flight.release(2));
    InFlightFrames<int> zero(0);
    CHECK(!zero.commit(1, std::make_shared<int>(1)));

    VideoBudget budget;
    VideoDemand demand{1920, 1080, 30, 32 * 1024 * 1024};
    auto reservation = budget.admit("gen1/camera/pool1", demand);
    CHECK(reservation);
    CHECK(budget.usage().streams == 1);
    CHECK(!budget.admit("gen1/screen/pool1", demand));
    CHECK(!budget.admit("gen1/camera/pool1", demand));
    reservation.reset();
    CHECK(budget.usage().streams == 0);
    CHECK(budget.usage().bytes == 0);
    CHECK(budget.usage().pixels_per_second == 0);
    CHECK(budget.admit("new", demand));
    CHECK(!budget.admit("", demand));
    auto bad = demand; bad.width = 0; CHECK(!budget.admit("a", bad));
    bad = demand; bad.fps = 0; CHECK(!budget.admit("a", bad));
    bad = demand; bad.fps = 31; CHECK(!budget.admit("a", bad));
    bad = demand; bad.pool_bytes = 0; CHECK(!budget.admit("a", bad));
    bad = demand; bad.pool_bytes = std::numeric_limits<std::uint64_t>::max();
    CHECK(!budget.admit("a", bad));

    VideoLimits limits;
    limits.streams = 3;
    limits.pixels_per_second = 1920ULL * 1080 * 30 - 1;
    VideoBudget not_enough_pixels(limits);
    CHECK(!not_enough_pixels.admit("a", demand));
    limits.pixels_per_second = 1920ULL * 1080 * 60;
    limits.bytes = demand.pool_bytes;
    VideoBudget not_enough_memory(limits);
    auto first = not_enough_memory.admit("a", demand);
    CHECK(first);
    CHECK(!not_enough_memory.admit("b", demand));
    CHECK(not_enough_memory.usage().streams == 1);
    first.reset();
    CHECK(not_enough_memory.usage().streams == 0);

    // Pool leases survive destruction of their admission controller.
    std::shared_ptr<const VideoBudget::Reservation> surviving;
    {
      VideoBudget temporary;
      surviving = temporary.admit("pool", demand);
      CHECK(surviving);
    }
    surviving.reset();
  });
}
