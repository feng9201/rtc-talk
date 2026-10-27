#pragma once

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QWidget>

namespace rtcwin::ui {

class SegmentedControl final : public QWidget {
  Q_OBJECT

 public:
  explicit SegmentedControl(bool compact = false, QWidget* parent = nullptr);

  void AddItem(const QString& id, const QString& text);
  void SetCurrentId(const QString& id);
  QString CurrentId() const;
  void SetEnabled(bool enabled);

 signals:
  void CurrentIdChanged(const QString& id);

 private:
  QButtonGroup* group_;
  QHBoxLayout* layout_;
  bool compact_;
};

}  // namespace rtcwin::ui
