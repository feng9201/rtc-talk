#pragma once

#include "base/Theme.h"

#include <QWidget>

class Pill : public QWidget
{
    Q_OBJECT

public:
    explicit Pill(const QString &text = QString(), const QString &tone = QStringLiteral("default"), QWidget *parent = nullptr);
    void setText(const QString &text);
    void setTone(const QString &tone);
    void setIcon(Theme::Icon icon, bool visible = true);
    void setClickable(bool clickable);
    QSize sizeHint() const override;

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QString m_text;
    QString m_tone;
    Theme::Icon m_icon = Theme::Icon::Info;
    bool m_hasIcon = false;
    bool m_clickable = false;
};