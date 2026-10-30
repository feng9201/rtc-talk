#pragma once

#include "rtcwin/domain/types.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QSpinBox;

namespace rtcwin::ui {

class RoomPolicyDialog final : public QDialog {
  Q_OBJECT

 public:
  explicit RoomPolicyDialog(const domain::RoomPolicy& policy,
                            QWidget* parent = nullptr);

 signals:
  void PolicyChanged(const rtcwin::domain::RoomPolicyPatch& patch);

 private:
  void BuildUi(const domain::RoomPolicy& policy);
  void Apply();

  QComboBox* talk_mode_ = nullptr;
  QSpinBox* max_speakers_ = nullptr;
  QSpinBox* ptt_hold_seconds_ = nullptr;
  QCheckBox* ptt_queue_ = nullptr;
  QComboBox* media_mode_ = nullptr;
  QCheckBox* p2p_allow_relay_ = nullptr;
  QSpinBox* max_peers_ = nullptr;
  QCheckBox* video_enabled_ = nullptr;
  QSpinBox* video_max_publishers_ = nullptr;
};

}  // namespace rtcwin::ui
