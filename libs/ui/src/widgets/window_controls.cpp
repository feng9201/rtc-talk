#include "rtcwin/ui/widgets/window_controls.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVariant>

namespace rtcwin::ui {

WindowControls::WindowControls(QWidget* parent) : QWidget(parent) {
  setProperty("noWindowDrag", true);
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(2);

  const auto add_button = [this, layout](const QString& text,
                                         const QString& kind,
                                         const QString& tooltip) {
    auto* button = new QPushButton(text, this);
    button->setProperty("kind", kind);
    button->setFixedSize(34, 28);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(tooltip);
    layout->addWidget(button);
    return button;
  };

  auto* minimize = add_button(QStringLiteral("−"), QStringLiteral("window"),
                              QStringLiteral("最小化"));
  auto* maximize = add_button(QStringLiteral("□"), QStringLiteral("window"),
                              QStringLiteral("最大化 / 还原"));
  auto* close = add_button(QStringLiteral("×"), QStringLiteral("windowClose"),
                           QStringLiteral("关闭"));

  connect(minimize, &QPushButton::clicked, this,
          &WindowControls::MinimizeRequested);
  connect(maximize, &QPushButton::clicked, this,
          &WindowControls::MaximizeRequested);
  connect(close, &QPushButton::clicked, this,
          &WindowControls::CloseRequested);
  setFixedSize(106, 28);
}

}  // namespace rtcwin::ui