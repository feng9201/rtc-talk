#pragma once

#include <QWidget>

class WindowControls : public QWidget
{
    Q_OBJECT

public:
    explicit WindowControls(QWidget *parent = nullptr);
    QSize sizeHint() const override;
};