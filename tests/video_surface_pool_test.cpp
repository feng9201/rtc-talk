#include "rtcwin/ui/video/video_surface_pool.h"

#include <QApplication>
#include <QWidget>

#include <iostream>

namespace {

int failures = 0;

void Expect(bool condition, const char* message) {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QWidget host;
  QWidget owner;
  rtcwin::ui::VideoSurfacePool pool(&host, 2);

  auto first = pool.Acquire(QStringLiteral("u1|camera"), &owner);
  auto second = pool.Acquire(QStringLiteral("u1|screen"), &owner);
  Expect(first != nullptr && second != nullptr,
         "pool did not acquire two surfaces");
  Expect(pool.active_count() == 2, "active surface count is incorrect");
  Expect(pool.Acquire(QStringLiteral("u2|camera"), &owner) == nullptr,
         "pool exceeded its configured capacity");
  Expect(first->surface()->parentWidget() == &owner,
         "leased surface was not reparented to its tile");

  first.reset();
  Expect(pool.active_count() == 1 && pool.idle_count() == 1,
         "released surface did not enter idle pool");
  auto reused = pool.Acquire(QStringLiteral("u2|camera"), &owner);
  Expect(reused != nullptr, "pool did not reuse an idle surface");
  Expect(pool.surface_count() == 2, "pool created a redundant surface");
  Expect(reused->surface() != second->surface(),
         "surface was assigned to two active leases");

  reused.reset();
  second.reset();
  Expect(pool.active_count() == 0 && pool.idle_count() == 2,
         "released pool surface counts are incorrect");
  pool.Shutdown();
  Expect(pool.active_count() == 0 && pool.idle_count() == 0,
         "shutdown did not clear pooled surfaces");

  if (failures != 0) {
    std::cerr << failures << " video surface pool test(s) failed\n";
    return 1;
  }
  std::cout << "video_surface_pool_test: PASS\n";
  return 0;
}
