#include "rtcwin/ui/widgets/device_combo_box.h"
#include "rtcwin/ui/widgets/device_panel.h"
#include "rtcwin/ui/theme/theme.h"
#include "rtcwin/ui/pages/room_page.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QTimer>
#include <QPointer>
#include <QEvent>
#include <QVBoxLayout>
#include <iostream>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  rtcwin::ui::theme::Apply(app);
  int failures = 0;
  auto check = [&failures](bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
  };
  QWidget host;
  host.setWindowTitle(QStringLiteral("RTC Talk Device Combo Test"));
  host.resize(800, 1100);
  auto* panel = new rtcwin::ui::DevicePanel(&host);
  panel->setGeometry(492, 8, 294, 1075);
  QVector<rtcwin::domain::AudioDevice> audio = {
      {"a", "Microphone (5- Logitech Webcam C925e)"},
      {"b", "Headphones (High Definition Audio Device) - Very Long Device Name"}};
  panel->SetDevices(audio, audio, {{"0", "Logitech Webcam C925e"}},
                    {{"1", "A very long screen/window source title that must not widen the sidebar", false}});
  host.show();
  app.processEvents();
  const auto combos = panel->findChildren<QComboBox*>();
  check(combos.size() == 7, "all device selectors and capture settings exist");
  for (auto* combo : combos) {
    const QRect geometry(combo->mapTo(panel, QPoint()), combo->size());
    check(panel->rect().contains(geometry), "long names stay inside the panel");
  }
  auto* combo = static_cast<rtcwin::ui::DeviceComboBox*>(combos.front());
  combo->setCurrentIndex(1);
  int selection_changes = 0;
  QObject::connect(panel, &rtcwin::ui::DevicePanel::RecordingDeviceSelected,
                   [&selection_changes] { ++selection_changes; });
  combo->showPopup();
  app.processEvents();
  panel->SetDevices(audio, audio, {}, {});
  app.processEvents();
  check(combo->view()->isVisible(), "inventory refresh does not dismiss popup");
  check(combo->currentData().toString() == "b", "refresh preserves selected ID");
  check(selection_changes == 0, "refresh does not switch device");
  audio.push_back({"c", "Newly attached microphone"});
  panel->SetDevices(audio, audio, {}, {});
  app.processEvents();
  check(combo->view()->isVisible() && combo->count() == 2,
        "changed inventory waits until popup closes");
  const QRect bounds(host.mapToGlobal(QPoint()), host.size());
  check(bounds.contains(combo->view()->window()->geometry()), "popup stays inside owner");
  combo->hidePopup();
  app.processEvents();
  check(combo->count() == 3 && combo->currentData().toString() == "b",
        "deferred update preserves selection");
  check(selection_changes == 0, "deferred update does not switch device");
  rtcwin::ui::DeviceComboBox bottom(&host);
  bottom.setGeometry(600, 740, 185, 44);
  rtcwin::ui::DeviceComboBox::Items items;
  for (int i = 0; i < 20; ++i) items.push_back({QString::number(i), i});
  bottom.SetItems(items, true);
  bottom.show();
  bottom.showPopup();
  app.processEvents();
  check(bounds.contains(bottom.view()->window()->geometry()), "bottom popup opens within owner");
  bottom.hidePopup();
  // Exercise the real room snapshot path, not just DevicePanel::SetDevices.
  rtcwin::ui::RoomPage room;
  room.setWindowTitle(QStringLiteral("RTC Talk Device Refresh Test"));
  room.resize(1440, 900);
  rtcwin::domain::SessionSnapshot state;
  state.self_uid = "self";
  state.join = rtcwin::domain::JoinPhase::Joined;
  state.connection = rtcwin::domain::ConnectionPhase::Ready;
  room.SetState(state);
  room.SetAudioDevices(audio, audio);
  room.SelectSideTab(rtcwin::domain::PanelTab::Devices);
  room.show();
  app.processEvents();
  QPointer<rtcwin::ui::DevicePanel> original_panel = room.findChild<rtcwin::ui::DevicePanel*>();
  QPointer<QComboBox> original_combo = original_panel->findChildren<QComboBox*>().front();
  original_combo->showPopup();
  app.processEvents();
  for (int i = 0; i < 12; ++i) {
    state.devices.recording_device = "b";
    state.peers.push_back({"peer-" + std::to_string(i), "peer"});
    room.SetState(state);
    room.SetAudioDevices(audio, audio);
    app.processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (!original_panel || !original_combo) break;
  }
  check(original_panel && original_combo, "room snapshots preserve device widgets");
  if (original_combo) {
    check(original_combo->view()->isVisible(), "room snapshots and peer changes preserve open popup");
    original_combo->hidePopup();
  }
  if (app.arguments().contains(QStringLiteral("--interactive"))) {
    QTimer refresh;
    QObject::connect(&refresh, &QTimer::timeout, &room, [&] {
      room.SetState(state);
      room.SetAudioDevices(audio, audio);
    });
    refresh.start(100);
    QTimer::singleShot(60000, &app, &QApplication::quit);
    app.exec();
  }
  std::cout << "device combo failures=" << failures << '\n';
  return failures ? 1 : 0;
}
