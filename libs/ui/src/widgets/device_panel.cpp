#include "rtcwin/ui/widgets/device_panel.h"

#include "rtcwin/ui/theme/theme.h"
#include "rtcwin/ui/widgets/device_combo_box.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace rtcwin::ui {
namespace {

QString Text(const std::string& value) {
  return QString::fromStdString(value);
}

}  // namespace

DevicePanel::DevicePanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("DevicePanel"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(9);

  recording_ = new DeviceComboBox(this);
  playout_ = new DeviceComboBox(this);
  system_audio_ = new DeviceComboBox(this);
  camera_ = new DeviceComboBox(this);
  desktop_source_ = new DeviceComboBox(this);

  layout->addWidget(BuildField(
      QStringLiteral("麦克风"), recording_,
      QStringLiteral("切换后保持当前房间和通话")));
  layout->addWidget(BuildField(
      QStringLiteral("播放设备"), playout_,
      QStringLiteral("远端声音输出设备")));
  layout->addWidget(BuildField(
      QStringLiteral("系统音采集端点"), system_audio_,
      QStringLiteral("仅系统音模式下生效，跟随播放设备列表")));
  layout->addWidget(BuildField(
      QStringLiteral("摄像头"), camera_,
      QStringLiteral("切换后会重启当前发布中的摄像头")));
  layout->addWidget(BuildField(
      QStringLiteral("屏幕/窗口共享源"), desktop_source_,
      QStringLiteral("选择后立即切换共享源")));

  camera_resolution_ = new DeviceComboBox(this);
  camera_resolution_->setObjectName(QStringLiteral("RoomCameraResolution"));
  camera_resolution_->setEnabled(false);
  layout->addWidget(BuildField(QStringLiteral("摄像头采集分辨率"), camera_resolution_,
                                QStringLiteral("已锁定，离房后可修改")));
  screen_resolution_ = new DeviceComboBox(this);
  screen_resolution_->setObjectName(QStringLiteral("RoomScreenResolution"));
  screen_resolution_->addItem(QStringLiteral("1080P（等比，最高 1920×1080）"), false);
  screen_resolution_->addItem(QStringLiteral("原始分辨率（所选屏幕/窗口）"), true);
  layout->addWidget(BuildField(QStringLiteral("桌面采集分辨率"), screen_resolution_,
                                QStringLiteral("共享期间不可修改")));
  connect(screen_resolution_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this] { emit ScreenResolutionSelected(screen_resolution_->currentData().toBool()); });

  refresh_ = theme::Button(QStringLiteral("重新扫描设备"),
                           QStringLiteral("ghost"), this);
  layout->addWidget(refresh_);
  status_ = theme::Label(QStringLiteral("设备列表由媒体引擎枚举"),
                         QStringLiteral("Hint"), this);
  status_->setWordWrap(true);
  layout->addWidget(status_);
  layout->addStretch(1);

  connect(refresh_, &QPushButton::clicked, this,
          &DevicePanel::RefreshRequested);
  connect(recording_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this] {
            if (recording_->currentIndex() >= 0 &&
                !recording_->currentData().toString().isEmpty()) {
              emit RecordingDeviceSelected(
                  recording_->currentData().toString());
            }
          });
  connect(playout_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this] {
            if (playout_->currentIndex() >= 0 &&
                !playout_->currentData().toString().isEmpty()) {
              emit PlayoutDeviceSelected(playout_->currentData().toString());
            }
          });
  connect(system_audio_,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this] {
            if (system_audio_->currentIndex() >= 0 &&
                !system_audio_->currentData().toString().isEmpty()) {
              emit SystemAudioDeviceSelected(
                  system_audio_->currentData().toString());
            }
          });
  connect(camera_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this] {
            bool ok = false;
            const int index = camera_->currentData().toInt(&ok);
            if (ok && index >= 0) emit CameraSelected(index);
          });
  connect(desktop_source_,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this] {
            const QString value = desktop_source_->currentData().toString();
            const int separator = value.indexOf(QLatin1Char(':'));
            if (separator <= 0) return;
            emit DesktopSourceSelected(
                value.mid(separator + 1),
                value.left(separator) == QStringLiteral("window"));
          });
}

QWidget* DevicePanel::BuildField(const QString& label, QComboBox* combo,
                                 const QString& hint) {
  auto* field = new QWidget(this);
  auto* layout = new QVBoxLayout(field);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(4);
  layout->addWidget(theme::Label(label, QStringLiteral("FieldLabel"), field));
  layout->addWidget(combo);
  if (!hint.isEmpty()) {
    auto* description = theme::Label(hint, QStringLiteral("Hint"), field);
    description->setWordWrap(true);
    layout->addWidget(description);
  }
  return field;
}

void DevicePanel::FillAudioCombo(
    QComboBox* combo, const QVector<rtcwin::domain::AudioDevice>& devices,
    const QString& empty_text) {
  DeviceComboBox::Items items;
  for (const auto& device : devices) {
    if (!device.available) continue;
    items.push_back({Text(device.name), Text(device.id)});
  }
  const bool enabled = !items.isEmpty();
  if (!enabled) items.push_back({empty_text, QString()});
  static_cast<DeviceComboBox*>(combo)->SetItems(std::move(items), enabled);
}

void DevicePanel::SelectData(QComboBox* combo, const QString& value) {
  if (combo == nullptr || value.isEmpty()) return;
  const QSignalBlocker blocker(combo);
  const int index = combo->findData(value);
  if (index >= 0) {
    combo->setCurrentIndex(index);
    combo->setToolTip(combo->currentText());
  }
}

void DevicePanel::SetDevices(
    const QVector<rtcwin::domain::AudioDevice>& recording,
    const QVector<rtcwin::domain::AudioDevice>& playout,
    const QVector<rtcwin::domain::CameraDevice>& cameras,
    const QVector<rtcwin::domain::DesktopSource>& desktop_sources) {
  FillAudioCombo(recording_, recording, QStringLiteral("未检测到麦克风"));
  FillAudioCombo(playout_, playout, QStringLiteral("未检测到播放设备"));
  FillAudioCombo(system_audio_, playout,
                 QStringLiteral("未检测到系统音端点"));
  SetSystemAudioEnabled(system_audio_enabled_);

  {
    DeviceComboBox::Items items;
    for (const auto& device : cameras) {
      bool ok = false;
      const int index = Text(device.id).toInt(&ok);
      if (ok && index >= 0 && device.available) {
        items.push_back({Text(device.name), index});
      }
    }
    const bool enabled = !items.isEmpty();
    if (!enabled) items.push_back({QStringLiteral("未检测到摄像头"), -1});
    static_cast<DeviceComboBox*>(camera_)->SetItems(std::move(items), enabled);
  }
  {
    DeviceComboBox::Items items;
    for (const auto& source : desktop_sources) {
      const QString data =
          (source.window ? QStringLiteral("window:")
                         : QStringLiteral("screen:")) +
          Text(source.id);
      items.push_back({
          (source.window ? QStringLiteral("窗口 · ")
                         : QStringLiteral("屏幕 · ")) +
              Text(source.title),
          data});
    }
    const bool enabled = !items.isEmpty();
    if (!enabled) items.push_back({QStringLiteral("未发现可共享源"), QString()});
    static_cast<DeviceComboBox*>(desktop_source_)->SetItems(std::move(items), enabled);
  }
}

void DevicePanel::SetSelection(
    const rtcwin::domain::DeviceSelection& selection) {
  SelectData(recording_, Text(selection.recording_device));
  SelectData(playout_, Text(selection.playout_device));
  SelectData(system_audio_, Text(selection.system_audio_device));
  if (!selection.camera_device.empty()) {
    const QString value = Text(selection.camera_device);
    const QSignalBlocker blocker(camera_);
    const int index = camera_->findData(value.toInt());
    if (index >= 0) {
      camera_->setCurrentIndex(index);
      camera_->setToolTip(camera_->currentText());
    }
  }
  if (!selection.screen_source.empty()) {
    const QString value =
        (selection.screen_source_window ? QStringLiteral("window:")
                                        : QStringLiteral("screen:")) +
        Text(selection.screen_source);
    SelectData(desktop_source_, value);
  }
}

void DevicePanel::SetSystemAudioEnabled(bool enabled) {
  system_audio_enabled_ = enabled;
  if (system_audio_ == nullptr) return;
  system_audio_->setEnabled(enabled && system_audio_->count() > 0 &&
                            !system_audio_->currentData().toString().isEmpty());
}

void DevicePanel::SetCaptureState(const domain::DeviceSelection& selection, bool sharing) {
  const QSignalBlocker camera_blocker(camera_resolution_);
  const QSignalBlocker screen_blocker(screen_resolution_);
  const QString text = QStringLiteral("%1×%2（入房前设置）")
                           .arg(selection.camera_width).arg(selection.camera_height);
  static_cast<DeviceComboBox*>(camera_resolution_)->SetItems({{text, QVariant()}}, false);
  screen_resolution_->setCurrentIndex(selection.screen_native_resolution ? 1 : 0);
  screen_resolution_->setEnabled(!sharing);
}
void DevicePanel::SetStatus(const QString& text) {
  if (status_ != nullptr) status_->setText(text);
}

}  // namespace rtcwin::ui
