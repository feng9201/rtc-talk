#include "rtcwin/ui/widgets/device_combo_box.h"

#include <QAbstractItemView>
#include <QSignalBlocker>
#include <QStylePainter>
#include <QStyleOptionComboBox>
#include <QTimer>
#include <algorithm>

namespace rtcwin::ui {

DeviceComboBox::DeviceComboBox(QWidget* parent) : QComboBox(parent) {
  setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  setMinimumContentsLength(0);
  setMinimumWidth(0);
  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  setMaxVisibleItems(8);
  view()->setTextElideMode(Qt::ElideRight);
  view()->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  connect(this, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this] { setToolTip(currentText()); });
}

void DeviceComboBox::SetItems(Items items, bool enabled) {
  // Device enumeration runs periodically. Never reset the active popup model.
  if (view()->isVisible()) {
    pending_items_ = std::move(items);
    pending_enabled_ = enabled;
    pending_ = true;
    return;
  }
  pending_ = false;
  bool same = count() == items.size();
  for (int i = 0; same && i < count(); ++i) {
    same = itemText(i) == items[i].first && itemData(i) == items[i].second;
  }
  const QSignalBlocker blocker(this);
  if (!same) {
    const QVariant selected = currentData();
    clear();
    for (const auto& item : items) {
      addItem(item.first, item.second);
      setItemData(count() - 1, item.first, Qt::ToolTipRole);
    }
    const int index = findData(selected);
    if (index >= 0) setCurrentIndex(index);
  }
  setEnabled(enabled);
  setToolTip(currentText());
}

void DeviceComboBox::showPopup() {
  QComboBox::showPopup();
  if (!view()->isVisible()) return;
  QWidget* popup = view()->window();
  QWidget* owner = window();
  const QRect bounds(owner->mapToGlobal(QPoint(0, 0)), owner->size());
  const QRect available = bounds.adjusted(4, 4, -4, -4);
  const QPoint below = mapToGlobal(QPoint(0, height()));
  const int above_y = mapToGlobal(QPoint(0, 0)).y();
  const int below_space = std::max(0, available.bottom() + 1 - below.y());
  const int above_space = std::max(0, above_y - available.top());
  const bool open_below = below_space >= popup->height() || below_space >= above_space;
  const int h = std::min(popup->height(), open_below ? below_space : above_space);
  const int w = std::min(width(), available.width());
  const int x = std::clamp(below.x(), available.left(), available.right() + 1 - w);
  popup->setMinimumWidth(0);
  popup->setGeometry(x, open_below ? below.y() : above_y - h, w, h);
}

void DeviceComboBox::paintEvent(QPaintEvent*) {
  QStylePainter painter(this);
  QStyleOptionComboBox option;
  initStyleOption(&option);
  const QRect text_rect = style()->subControlRect(
      QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, this);
  option.currentText = fontMetrics().elidedText(
      option.currentText, Qt::ElideRight, text_rect.width());
  painter.drawComplexControl(QStyle::CC_ComboBox, option);
  painter.drawControl(QStyle::CE_ComboBoxLabel, option);
}

void DeviceComboBox::hidePopup() {
  QComboBox::hidePopup();
  // Let QComboBox finish committing the clicked row before applying new items.
  QTimer::singleShot(0, this, [this] {
    if (!pending_ || view()->isVisible()) return;
    pending_ = false;
    // A join/share transition may have disabled the control while it was open.
    SetItems(std::move(pending_items_), pending_enabled_ && isEnabled());
  });
}

}  // namespace rtcwin::ui
