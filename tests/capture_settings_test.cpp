#include "rtcwin/ui/pages/prejoin_page.h"
#include "rtcwin/ui/widgets/device_panel.h"
#include "rtcwin/ui/theme/theme.h"
#include <QApplication>
#include <QComboBox>
#include <iostream>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  rtcwin::ui::theme::Apply(app);
  int failures = 0;
  auto check = [&](bool value, const char* message) {
    if (!value) { ++failures; std::cerr << message << '\n'; }
  };
  rtcwin::ui::PrejoinPage page;
  rtcwin::domain::PrejoinOptions options;
  options.camera_device = "0";
  page.SetOptions(options);
  rtcwin::domain::CameraDevice camera{"0", "Test camera", true,
      {{640, 480}, {1280, 720}, {1920, 1080}, {3840, 2160}}};
  page.SetCameraDevices({camera});
  auto* resolution = page.findChild<QComboBox*>(QStringLiteral("CameraResolution"));
  auto* desktop = page.findChild<QComboBox*>(QStringLiteral("ScreenResolution"));
  check(resolution && desktop, "capture controls exist");
  if (!resolution || !desktop) return 1;
  check(resolution->count() == 3, "720p, 1080p and maximum only");
  check(page.Options().camera_width == 1280, "720p default");
  resolution->setCurrentIndex(resolution->findData(QSize(3840, 2160)));
  check(page.Options().camera_width == 3840 && page.Options().camera_height == 2160,
        "maximum reaches join options");
  desktop->setCurrentIndex(1);
  check(page.Options().screen_native_resolution, "native desktop reaches join options");
  page.SetBusy(true);
  check(!resolution->isEnabled() && !desktop->isEnabled(), "joining locks settings");
  page.SetBusy(false);
  check(resolution->isEnabled() && desktop->isEnabled(), "leaving unlocks settings");
  page.SetCameraDevices({{"0", "Test camera"}});
  check(resolution->count() == 3 && page.Options().camera_width == 3840,
        "temporary busy-device enumeration preserves capabilities and selection");
  camera.id = "1";
  camera.name = "720p only camera";
  camera.resolutions = {{640, 480}, {1280, 720}};
  page.SetCameraDevices({camera});
  check(resolution->count() == 1 && page.Options().camera_width == 1280,
        "new device removes unsupported modes and deduplicates maximum");
  options.camera_device = "1";
  options.camera_width = 1280;
  page.SetOptions(options);
  rtcwin::domain::CameraDevice other{"2", "1080p only", true, {{1920, 1080}}};
  page.SetCameraDevices({camera, other});
  for (auto* combo : page.findChildren<QComboBox*>()) {
    if (combo != resolution && combo != desktop && combo->findText(QStringLiteral("1080p only")) >= 0) {
      combo->setCurrentIndex(combo->findText(QStringLiteral("1080p only")));
      break;
    }
  }
  page.SetCameraDevices({camera, other});
  check(page.Options().camera_device == "2" && page.Options().camera_width == 1920,
        "inventory refresh retains a manually selected camera and its supported resolution");
  rtcwin::ui::DevicePanel panel;
  auto* room_camera = panel.findChild<QComboBox*>(QStringLiteral("RoomCameraResolution"));
  auto* room_desktop = panel.findChild<QComboBox*>(QStringLiteral("RoomScreenResolution"));
  rtcwin::domain::DeviceSelection selection;
  selection.camera_width = 1920;
  selection.camera_height = 1080;
  selection.screen_native_resolution = true;
  panel.SetCaptureState(selection, true);
  check(!room_camera->isEnabled() && !room_desktop->isEnabled(), "sharing locks both controls");
  check(room_camera->currentText().contains(QStringLiteral("1920")), "room shows locked capture size");
  panel.SetCaptureState(selection, false);
  check(!room_camera->isEnabled() && room_desktop->isEnabled(), "stopping share unlocks desktop only");
  check(room_desktop->currentData().toBool(), "room preserves native policy");
  return failures ? 1 : 0;
}
