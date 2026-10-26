#pragma once

#include "domain/app_types.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QSpinBox;

namespace talk::desktop {

class RoomPolicyDialog final : public QDialog {
  Q_OBJECT

 public:
  explicit RoomPolicyDialog(QWidget* parent = nullptr);

  void SetPolicy(const RoomPolicy& policy);
  RoomPolicy policy() const;

 private:
  QComboBox* talk_mode_ = nullptr;
  QComboBox* media_mode_ = nullptr;
  QSpinBox* max_speakers_ = nullptr;
  QSpinBox* ptt_hold_seconds_ = nullptr;
  QCheckBox* ptt_queue_ = nullptr;
  QCheckBox* p2p_relay_ = nullptr;
  QSpinBox* max_peers_ = nullptr;
  QCheckBox* video_enabled_ = nullptr;
  QSpinBox* video_max_publishers_ = nullptr;
};

}  // namespace talk::desktop
