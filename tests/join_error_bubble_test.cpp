#include "rtcwin/ui/main_window.h"
#include "rtcwin/ui/theme/theme.h"
#include "widgets/msg_box/BubbleMsg.h"

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QIcon>
#include <QPixmap>
#include <QPushButton>
#include <iostream>

namespace {
int failures = 0;
void Check(bool value, const char* message) {
  if (value) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}
bool HasText(QWidget* widget, const QString& text) {
  for (auto* label : widget->findChildren<QLabel*>()) {
    if (label->text().contains(text)) return true;
  }
  return false;
}
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  rtcwin::ui::theme::Apply(app);
  rtcwin::ui::MainWindow window;
  window.resize(1440, 900);
  window.show();
  app.processEvents();
  auto* bubble = window.findChild<BubbleMsgWithClose*>(QStringLiteral("JoinErrorBubble"));
  Check(bubble && !bubble->isVisible(), "no error before a join attempt");
  if (!bubble) return 1;

  rtcwin::domain::PrejoinOptions options;
  window.BeginJoin(options);  // Invalid URL: synchronous application validation.
  Check(bubble->isVisible() && HasText(bubble, QStringLiteral("信令地址无效")),
        "join validation error appears in the bubble");
  QPushButton* join = nullptr;
  for (auto* button : window.findChildren<QPushButton*>()) {
    if (button->text() == QStringLiteral("进入房间")) join = button;
  }
  Check(join && join->isEnabled(), "validation failure permits retry");

  options.signaling_url = "ws://127.0.0.1:1/talk/v1/ws";
  window.BeginJoin(options);  // Missing UID/room: still no network or settings writes.
  Check(HasText(bubble, QStringLiteral("用户标识和房间 ID 不能为空")),
        "retry replaces the previous error");
  Check(window.findChildren<BubbleMsgWithClose*>().size() == 1,
        "retry does not accumulate bubbles");
  window.resize(1200, 800);
  app.processEvents();
  Check(qAbs(bubble->geometry().center().x() - window.rect().center().x()) <= 1,
        "bubble stays centered after resize");
  const auto close_buttons = bubble->findChildren<QPushButton*>();
  Check(close_buttons.size() == 1 && !close_buttons.front()->icon().isNull(),
        "close icon is bundled");
  if (!close_buttons.empty()) close_buttons.front()->click();
  Check(!bubble->isVisible(), "close button dismisses the error");

  const QString long_error = QStringLiteral("入房失败：<server-error> ") +
      QStringLiteral("服务器拒绝加入此房间，请检查房间权限和凭证。").repeated(5);
  bubble->showMessage(BubbleType::Error, long_error);
  Check(bubble->height() > 40, "long error wraps instead of being clipped");
  Check(HasText(bubble, long_error), "complete error is retained");
  for (auto* label : bubble->findChildren<QLabel*>()) {
    if (label->text() == long_error) {
      Check(label->textFormat() == Qt::PlainText, "server text cannot become rich text");
    }
  }
  if (app.arguments().contains(QStringLiteral("--snapshot"))) {
    const int index = app.arguments().indexOf(QStringLiteral("--snapshot"));
    if (index + 1 >= app.arguments().size()) return 2;
    window.resize(1440, 900);
    app.processEvents();
    Check(window.grab().save(app.arguments().at(index + 1)), "save UI evidence");
  }
  std::cout << (failures ? "join error bubble: FAIL\n" : "join error bubble: PASS\n");
  return failures ? 1 : 0;
}
