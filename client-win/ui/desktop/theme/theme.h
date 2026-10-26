#pragma once

// Production desktop UI theme and icon factory.

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

class QApplication;
class QFrame;
class QLabel;
class QPushButton;
class QWidget;
class QPainter;
class QRectF;

namespace talk::desktop::Theme {

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
    Refresh,
    More,
    Crown,
    Grid,
    Focus,
    Close,
    Check,
    Alert,
    Info,
    Signal,
    Clock,
    ChevronDown,
    Diagnostics
};

void apply(QApplication &app);
void drawIcon(QPainter &painter, Icon icon, const QRectF &rect, const QColor &color, qreal stroke = 1.8);
QPixmap pixmap(Icon icon, int size, const QColor &color = QColor(QStringLiteral("#e8edf2")), qreal stroke = 1.8);

QFrame *card(QWidget *parent = nullptr, const QString &objectName = QStringLiteral("Card"));
QLabel *label(const QString &text, const QString &objectName = QStringLiteral("Body"), QWidget *parent = nullptr);
QPushButton *button(const QString &text, const QString &kind = QStringLiteral("default"), QWidget *parent = nullptr);
QWidget *separator(QWidget *parent = nullptr);

QString avatarText(const QString &name);
QString formatDuration(int seconds);

}  // namespace talk::desktop::Theme
