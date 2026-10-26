#pragma once

#include "domain/app_types.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QImage;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;

namespace talk::desktop {

class SegmentedControl;
class VideoSurface;

class PrejoinPage final : public QWidget {
  Q_OBJECT

 public:
  explicit PrejoinPage(QWidget* parent = nullptr);

  PrejoinOptions options() const;
  void SetOptions(const PrejoinOptions& options);
  void setJoinEnabled(bool enabled);
  void setStatusMessage(const QString& text, const QString& tone = QString());
  void SetCameraDevices(const QVector<CameraDevice>& devices);
  void SetPreviewFrame(const QImage& frame);
  void SetPreviewActive(bool active, const QString& device = QString());

 signals:
  void joinRequested(const PrejoinOptions& options);
  void saveSettingsRequested(const PrejoinOptions& options);
  void cameraRefreshRequested();
  void previewToggleRequested();
  void cameraPreviewRequested(int camera_index);

 private:
  QWidget* BuildHeader();
  QWidget* BuildFormCard();
  QWidget* BuildPreviewColumn();
  QWidget* MakeField(const QString& label, QWidget* editor,
                     const QString& hint = QString());
  QWidget* MakePair(QWidget* left, QWidget* right);
  void RefreshSummary();

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
  VideoSurface* preview_surface_ = nullptr;
  QPushButton* preview_ = nullptr;
  QLabel* preview_device_ = nullptr;
  QSlider* volume_ = nullptr;
  QLabel* volume_value_ = nullptr;
  QLabel* summary_connection_ = nullptr;
  QLabel* summary_audio_ = nullptr;
  QLabel* summary_video_ = nullptr;
  QLabel* summary_policy_ = nullptr;
  QLabel* status_ = nullptr;
  QPushButton* join_ = nullptr;
  QPushButton* save_ = nullptr;
  bool preview_active_ = false;
  int preferred_camera_index_ = -1;
};

}  // namespace talk::desktop
