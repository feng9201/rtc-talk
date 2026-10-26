#include "widgets/control_button.h"

#include "widgets/icon_button.h"

#include <QLabel>
#include <QVBoxLayout>

namespace talk::desktop {

ControlButton::ControlButton(Theme::Icon icon, const QString &text, QWidget *parent)
    : QWidget(parent), m_button(new IconButton(icon, this)), m_label(new QLabel(text, this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);
    layout->setAlignment(Qt::AlignHCenter);
    layout->addWidget(m_button, 0, Qt::AlignHCenter);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setObjectName(QStringLiteral("ControlLabel"));
    layout->addWidget(m_label);
    connect(m_button, &IconButton::clicked, this, &ControlButton::clicked);
    connect(m_button, &IconButton::pressed, this, &ControlButton::pressed);
    connect(m_button, &IconButton::released, this, &ControlButton::released);
}

void ControlButton::setIcon(Theme::Icon icon) { m_button->setIcon(icon); }
void ControlButton::setText(const QString &text) { m_label->setText(text); }
void ControlButton::setTone(const QString &tone) { m_button->setTone(tone); }
void ControlButton::setEnabledState(bool enabled) { m_button->setEnabled(enabled); }

}  // namespace talk::desktop
