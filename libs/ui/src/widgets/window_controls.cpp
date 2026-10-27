#include "rtcwin/ui/widgets/window_controls.h"

#include "rtcwin/ui/theme/theme.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVariant>

namespace rtcwin::ui {

WindowControls::WindowControls(QWidget* parent) : QWidget(parent) {
  setProperty("noWindowDrag", true);
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(2);

  const auto add_button = [this, layout](theme::Icon icon, const QString& kind,
                                         const QString& tooltip) {
    auto* button = new QPushButton(this);
    button->setProperty("kind", kind);
    button->setIcon(QIcon(theme::Pixmap(icon, 15)));
    button->setIconSize(QSize(15, 15));
    button->setFixedSize(36, 28);
    button->setToolTip(tooltip);
    button->setCursor(Qt::PointingHandCursor);
    layout->addWidget(button);
    return button;
  };

  auto* minimize = add_button(theme::Icon::Minimize,
                              QStringLiteral("window"),
                              QStringLiteral("最小化"));
  auto* maximize = add_button(theme::Icon::Maximize,
                              QStringLiteral("window"),
                              QStringLiteral("最大化"));
  auto* close = add_button(theme::Icon::Close,
                           QStringLiteral("windowClose"),
                           QStringLiteral("关闭"));

  connect(minimize, &QPushButton::clicked, this,
          &WindowControls::MinimizeRequested);
  connect(maximize, &QPushButton::clicked, this,
          &WindowControls::MaximizeRequested);
  connect(close, &QPushButton::clicked, this,
          &WindowControls::CloseRequested);
}

}  // namespace rtcwin::ui
