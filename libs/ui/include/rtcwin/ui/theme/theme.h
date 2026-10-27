#pragma once

#include <QColor>
#include <QPixmap>
#include <QString>

class QApplication;
class QFrame;
class QLabel;
class QPushButton;
class QWidget;

namespace rtcwin::ui::theme {

enum class Icon {
  Mic,
  MicOff,
  Camera,
  CameraOff,
  Screen,
  Settings,
  Leave,
  Users,
  Headphones,
  HeadphonesOff,
  Grid,
  Focus,
  Close,
  Maximize,
  Minimize,
  Crown,
  Clock,
  Refresh,
  More,
};

void Apply(QApplication& app);
QPixmap Pixmap(Icon icon, int size,
               const QColor& color = QColor(QStringLiteral("#e7eef4")));

QFrame* Card(QWidget* parent, const QString& object_name = QStringLiteral("Card"));
QLabel* Label(const QString& text, const QString& object_name,
              QWidget* parent = nullptr);
QPushButton* Button(const QString& text, const QString& kind,
                    QWidget* parent = nullptr);
QString AvatarText(const QString& name);
QString FormatDuration(qint64 seconds);

}  // namespace rtcwin::ui::theme
