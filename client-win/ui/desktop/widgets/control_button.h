#pragma once

#include "theme/theme.h"

#include <QLabel>
#include <QWidget>

namespace talk::desktop {

class IconButton;

class ControlButton : public QWidget
{
    Q_OBJECT

public:
    explicit ControlButton(Theme::Icon icon, const QString &text, QWidget *parent = nullptr);
    void setIcon(Theme::Icon icon);
    void setText(const QString &text);
    void setTone(const QString &tone);
    IconButton *button() const { return m_button; }
    void setEnabledState(bool enabled);

signals:
    void clicked();
    void pressed();
    void released();

private:
    IconButton *m_button;
    QLabel *m_label;
};

}  // namespace talk::desktop
