#pragma once

#include <QWidget>

class Avatar : public QWidget
{
    Q_OBJECT

public:
    explicit Avatar(QWidget *parent = nullptr);
    Avatar(const QString &text, int diameter, QWidget *parent = nullptr);
    void setText(const QString &text);
    void setDiameter(int diameter);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_text;
    int m_diameter = 40;
};