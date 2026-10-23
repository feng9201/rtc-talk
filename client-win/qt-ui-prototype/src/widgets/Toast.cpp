#include "widgets/Toast.h"

#include "base/Theme.h"

#include <QGraphicsOpacityEffect>
#include <QHash>
#include <QLabel>
#include <QPointer>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QHash<QWidget *, QPointer<Toast>> g_toasts;
}

Toast::Toast(QWidget *parent, const QString &text, const QString &tone)
    : QFrame(parent), m_label(new QLabel(text, this))
{
    setAttribute(Qt::WA_StyledBackground, true);
    setProperty("tone", tone);
    setStyleSheet(tone == QStringLiteral("error")
        ? QStringLiteral("Toast{background:#2a1717;border:1px solid #6a3535;border-radius:12px;} QLabel{color:#ffadad;font-weight:600;}")
        : tone == QStringLiteral("warn")
          ? QStringLiteral("Toast{background:#2b2114;border:1px solid #5d4725;border-radius:12px;} QLabel{color:#ffd08a;font-weight:600;}")
          : tone == QStringLiteral("ok")
            ? QStringLiteral("Toast{background:#12271d;border:1px solid #2e5a43;border-radius:12px;} QLabel{color:#9ce9bf;font-weight:600;}")
            : QStringLiteral("Toast{background:#18222b;border:1px solid #34495c;border-radius:12px;} QLabel{color:#c9d6e0;font-weight:600;}"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(15, 9, 15, 9);
    layout->addWidget(m_label);
    adjustSize();
}

void Toast::showMessage(QWidget *parent, const QString &text, const QString &tone)
{
    if (!parent) return;
    if (g_toasts.contains(parent)) {
        Toast *old = g_toasts.value(parent).data();
        g_toasts.remove(parent);
        if (old) old->deleteLater();
    }
    auto *toast = new Toast(parent, text, tone);
    g_toasts.insert(parent, toast);
    toast->reposition();
    toast->show();
    toast->raise();
    QTimer::singleShot(2600, toast, [parent, toast] {
        if (g_toasts.value(parent).data() == toast) g_toasts.remove(parent);
        toast->deleteLater();
    });
}

void Toast::dismissFor(QWidget *parent)
{
    Toast *toast = g_toasts.take(parent).data();
    if (toast) toast->deleteLater();
}

void Toast::reposition()
{
    if (!parentWidget()) return;
    adjustSize();
    move((parentWidget()->width() - width()) / 2, parentWidget()->height() - height() - 26);
}