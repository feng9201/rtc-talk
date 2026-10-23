#pragma once

#include "base/Theme.h"

#include <QWidget>

class IconButton;
class QLabel;

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

private:
    IconButton *m_button;
    QLabel *m_label;
};