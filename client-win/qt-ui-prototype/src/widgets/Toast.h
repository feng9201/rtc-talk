#pragma once

#include <QFrame>

class QLabel;

class Toast : public QFrame
{
    Q_OBJECT

public:
    static void showMessage(QWidget *parent, const QString &text, const QString &tone = QStringLiteral("info"));
    static void dismissFor(QWidget *parent);

private:
    Toast(QWidget *parent, const QString &text, const QString &tone);
    void reposition();
    QLabel *m_label;
};