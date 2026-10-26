#include "widgets/toast.h"

#include <QHash>
#include <QLabel>
#include <QPointer>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>

namespace talk::desktop {
namespace {

QHash<QWidget*, QPointer<Toast>> g_toasts;

}  // namespace

Toast::Toast(QWidget* parent, const QString& text, const QString& tone)
    : QFrame(parent), label_(new QLabel(text, this)) {
  setObjectName(QStringLiteral("Toast"));
  setProperty("tone", tone);
  setAttribute(Qt::WA_StyledBackground, true);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(15, 9, 15, 9);
  layout->addWidget(label_);
  adjustSize();
}

void Toast::Show(QWidget* parent, const QString& text, const QString& tone) {
  if (parent == nullptr || text.isEmpty()) return;
  Toast* old = g_toasts.take(parent).data();
  if (old != nullptr) old->deleteLater();
  auto* toast = new Toast(parent, text, tone);
  g_toasts.insert(parent, toast);
  toast->Reposition();
  toast->show();
  toast->raise();
  QTimer::singleShot(2800, toast, [parent, toast] {
    if (g_toasts.value(parent).data() == toast) g_toasts.remove(parent);
    toast->deleteLater();
  });
}

void Toast::Reposition() {
  if (parentWidget() == nullptr) return;
  adjustSize();
  move((parentWidget()->width() - width()) / 2,
       parentWidget()->height() - height() - 26);
}

}  // namespace talk::desktop
