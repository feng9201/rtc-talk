#pragma once

#include "theme/theme.h"

#include <QAbstractButton>

namespace talk::desktop {

class IconButton : public QAbstractButton
{
    Q_OBJECT

public:
    explicit IconButton(Theme::Icon icon, QWidget *parent = nullptr);
    void setIcon(Theme::Icon icon);
    void setTone(const QString &tone);
    void setDiameter(int diameter);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    Theme::Icon m_icon;
    QString m_tone = QStringLiteral("default");
    int m_diameter = 42;
    bool m_hovered = false;
};

}  // namespace talk::desktop
