#include "widgets/segmented_control.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVariant>

namespace talk::desktop {

SegmentedControl::SegmentedControl(bool compact, QWidget *parent)
    : QWidget(parent), m_group(new QButtonGroup(this)), m_layout(new QHBoxLayout(this)), m_compact(compact)
{
    setObjectName(QStringLiteral("SegmentedControl"));
    setAttribute(Qt::WA_StyledBackground, true);
    m_layout->setContentsMargins(3, 3, 3, 3);
    m_layout->setSpacing(3);
    m_group->setExclusive(true);
    connect(m_group, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked), this, [this](QAbstractButton *button) {
        emit currentIdChanged(button->property("segmentId").toString());
    });
}

void SegmentedControl::addItem(const QString &id, const QString &text)
{
    auto *button = new QPushButton(text, this);
    button->setCheckable(true);
    button->setProperty("kind", QStringLiteral("segment"));
    button->setProperty("segmentId", id);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(m_compact ? 28 : 34);
    button->setFocusPolicy(Qt::NoFocus);
    m_group->addButton(button);
    m_layout->addWidget(button);
    if (m_group->buttons().size() == 1) button->setChecked(true);
}

void SegmentedControl::setCurrentId(const QString &id)
{
    for (QAbstractButton *button : m_group->buttons()) {
        if (button->property("segmentId").toString() == id) {
            button->setChecked(true);
            return;
        }
    }
}

QString SegmentedControl::currentId() const
{
    if (auto *button = m_group->checkedButton())
        return button->property("segmentId").toString();
    return QString();
}

void SegmentedControl::setItemEnabled(const QString &id, bool enabled)
{
    for (QAbstractButton *button : m_group->buttons()) {
        if (button->property("segmentId").toString() == id)
            button->setEnabled(enabled);
    }
}

}  // namespace talk::desktop
