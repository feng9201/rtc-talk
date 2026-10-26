#pragma once

#include <QWidget>

namespace talk::desktop {

class WindowControls : public QWidget
{
    Q_OBJECT

public:
    explicit WindowControls(QWidget *parent = nullptr);
    QSize sizeHint() const override;
};

}  // namespace talk::desktop
