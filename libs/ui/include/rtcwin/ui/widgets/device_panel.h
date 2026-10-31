#pragma once

#include "rtcwin/domain/types.h"

#include <QVector>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;

namespace rtcwin::ui {

class DevicePanel final : public QWidget {
  Q_OBJECT

 public:
  explicit DevicePanel(QWidget* parent = nullptr);

  void SetDevices(
      const QVector<rtcwin::domain::AudioDevice>& recording,
      const QVector<rtcwin::domain::AudioDevice>& playout,
      const QVector<rtcwin::domain::CameraDevice>& cameras,
      const QVector<rtcwin::domain::DesktopSource>& desktop_sources);
  void SetSelection(const rtcwin::domain::DeviceSelection& selection);
  void SetSystemAudioEnabled(bool enabled);
  void SetStatus(const QString& text);

 signals:
  void RefreshRequested();
  void RecordingDeviceSelected(const QString& device_id);
  void PlayoutDeviceSelected(const QString& device_id);
  void SystemAudioDeviceSelected(const QString& device_id);
  void CameraSelected(int device_index);
  void DesktopSourceSelected(const QString& source_id, bool window);

 private:
  QWidget* BuildField(const QString& label, QComboBox* combo,
                      const QString& hint = {});
  static void FillAudioCombo(
      QComboBox* combo,
      const QVector<rtcwin::domain::AudioDevice>& devices,
      const QString& empty_text);
  static void SelectData(QComboBox* combo, const QString& value);

  QComboBox* recording_ = nullptr;
  QComboBox* playout_ = nullptr;
  QComboBox* system_audio_ = nullptr;
  QComboBox* camera_ = nullptr;
  QComboBox* desktop_source_ = nullptr;
  QLabel* status_ = nullptr;
  QPushButton* refresh_ = nullptr;
  bool system_audio_enabled_ = false;
};

}  // namespace rtcwin::ui
