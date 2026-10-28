#pragma once

#include "rtcwin/domain/types.h"

#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;

namespace rtcwin::muxplayer_adapter {
class MuxVideoSurface;
}

namespace rtcwin::ui {

class SegmentedControl;

class PrejoinPage final : public QWidget {
  Q_OBJECT

 public:
  explicit PrejoinPage(QWidget* parent = nullptr);

  domain::PrejoinOptions Options() const;
  void SetOptions(const domain::PrejoinOptions& options);
  void SetBusy(bool busy);
  void SetStatus(const QString& text, const QString& tone = {});

  void SetCameraDevices(
      const QVector<rtcwin::domain::CameraDevice>& devices);
  void SetPreviewActive(bool active, const QString& device, int width,
                        int height, int fps);
  void SetPreviewFrame(const rtcwin::domain::VideoFrame& frame);
  void SetPreviewPending(const QString& text);
  int SelectedCameraIndex() const;
  bool PreviewActive() const { return preview_active_; }
  bool PreviewPending() const { return preview_pending_; }

 signals:
  void JoinRequested(const rtcwin::domain::PrejoinOptions& options);
  void SaveRequested(const rtcwin::domain::PrejoinOptions& options);
  void PreviewToggleRequested(bool start);
  void CameraSelectionChanged(int device_index);
  void CameraRefreshRequested();

 private:
  QWidget* BuildHeader();
  QWidget* BuildFormCard();
  QWidget* BuildPreviewColumn();
  QWidget* MakeField(const QString& label, QWidget* editor,
                     const QString& hint = {});
  QWidget* MakePair(QWidget* left, QWidget* right);
  void TogglePreview();
  void UpdatePreviewControls();
  void UpdateSummary();

  QLineEdit* url_ = nullptr;
  QLineEdit* user_id_ = nullptr;
  QLineEdit* room_id_ = nullptr;
  QLineEdit* token_ = nullptr;
  QCheckBox* dev_token_ = nullptr;
  QComboBox* audio_backend_ = nullptr;
  QComboBox* media_mode_ = nullptr;
  QComboBox* talk_mode_ = nullptr;
  SegmentedControl* audio_source_ = nullptr;
  QComboBox* recording_device_ = nullptr;
  QComboBox* playout_device_ = nullptr;
  QComboBox* camera_device_ = nullptr;
  QSlider* volume_ = nullptr;
  QLabel* volume_value_ = nullptr;
  QLabel* preview_device_ = nullptr;
  QLabel* summary_connection_ = nullptr;
  QLabel* summary_audio_ = nullptr;
  QLabel* summary_video_ = nullptr;
  QLabel* summary_policy_ = nullptr;
  QLabel* status_ = nullptr;
  QPushButton* preview_button_ = nullptr;
  QPushButton* rescan_button_ = nullptr;
  QPushButton* join_button_ = nullptr;
  QPushButton* save_button_ = nullptr;
  rtcwin::muxplayer_adapter::MuxVideoSurface* preview_surface_ = nullptr;
  QString preferred_camera_id_;
  bool preview_active_ = false;
  bool preview_pending_ = false;
  bool busy_ = false;
};

}  // namespace rtcwin::ui