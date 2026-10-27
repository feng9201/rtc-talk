#include "rtcwin/ui/widgets/segmented_control.h"

#include <QPushButton>
#include <QVariant>

namespace rtcwin::ui {

SegmentedControl::SegmentedControl(bool compact, QWidget* parent)
    : QWidget(parent),
      group_(new QButtonGroup(this)),
      layout_(new QHBoxLayout(this)),
      compact_(compact) {
  setObjectName(QStringLiteral("SegmentedControl"));
  setAttribute(Qt::WA_StyledBackground, true);
  layout_->setContentsMargins(3, 3, 3, 3);
  layout_->setSpacing(3);
  group_->setExclusive(true);
  connect(group_, QOverload<QAbstractButton*>::of(&QButtonGroup::buttonClicked),
          this, [this](QAbstractButton* button) {
            emit CurrentIdChanged(
                button->property("segmentId").toString());
          });
}

void SegmentedControl::AddItem(const QString& id, const QString& text) {
  auto* button = new QPushButton(text, this);
  button->setCheckable(true);
  button->setProperty("kind", QStringLiteral("segment"));
  button->setProperty("segmentId", id);
  button->setCursor(Qt::PointingHandCursor);
  button->setFocusPolicy(Qt::NoFocus);
  button->setMinimumHeight(compact_ ? 27 : 32);
  group_->addButton(button);
  layout_->addWidget(button);
  if (group_->buttons().size() == 1) button->setChecked(true);
}

void SegmentedControl::SetCurrentId(const QString& id) {
  for (QAbstractButton* button : group_->buttons()) {
    if (button->property("segmentId").toString() == id) {
      button->setChecked(true);
      return;
    }
  }
}

QString SegmentedControl::CurrentId() const {
  if (auto* button = group_->checkedButton()) {
    return button->property("segmentId").toString();
  }
  return {};
}

void SegmentedControl::SetEnabled(bool enabled) {
  for (QAbstractButton* button : group_->buttons()) {
    button->setEnabled(enabled);
  }
}

}  // namespace rtcwin::ui
