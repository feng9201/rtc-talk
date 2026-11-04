#pragma once

#include <QComboBox>
#include <QVector>

namespace rtcwin::ui {

class DeviceComboBox final : public QComboBox {
 public:
  using Items = QVector<QPair<QString, QVariant>>;
  explicit DeviceComboBox(QWidget* parent = nullptr);
  void SetItems(Items items, bool enabled);
  void showPopup() override;
  void hidePopup() override;

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  Items pending_items_;
  bool pending_ = false;
  bool pending_enabled_ = true;
};

}  // namespace rtcwin::ui
