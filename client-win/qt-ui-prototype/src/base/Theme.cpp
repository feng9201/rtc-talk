#include "base/Theme.h"

#include <QApplication>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPushButton>
#include <QStyleFactory>

#include <cmath>

namespace Theme {
namespace {

QPointF point(const QRectF &rect, qreal x, qreal y)
{
    return QPointF(rect.left() + x * rect.width() / 24.0,
                   rect.top() + y * rect.height() / 24.0);
}

void drawPolyline(QPainter &painter, const QRectF &rect, const QVector<QPointF> &points)
{
    QPainterPath path(point(rect, points.first().x(), points.first().y()));
    for (int i = 1; i < points.size(); ++i)
        path.lineTo(point(rect, points[i].x(), points[i].y()));
    painter.drawPath(path);
}

} // namespace

void apply(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#0b0d10")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#e8edf2")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#0c1218")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#11171d")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#e8edf2")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#151e27")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#e8edf2")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#55c2ff")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#04141c")));
    palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#5c6b79")));
    app.setPalette(palette);

    QFile file(QStringLiteral(":/theme.qss"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        app.setStyleSheet(QString::fromUtf8(file.readAll()));
}

void drawIcon(QPainter &painter, Icon icon, const QRectF &rect, const QColor &color, qreal stroke)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    QPen pen(color, qMax<qreal>(1.0, stroke), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);

    const auto line = [&](qreal x1, qreal y1, qreal x2, qreal y2) {
        painter.drawLine(point(rect, x1, y1), point(rect, x2, y2));
    };
    const auto rounded = [&](qreal x, qreal y, qreal w, qreal h, qreal r) {
        painter.drawRoundedRect(QRectF(point(rect, x, y), point(rect, x + w, y + h)), r * rect.width() / 24.0,
                                r * rect.height() / 24.0);
    };

    switch (icon) {
    case Icon::Mic:
    case Icon::MicOff: {
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        QPainterPath mic;
        mic.addRoundedRect(QRectF(point(rect, 9, 3), point(rect, 15, 15)), 3.0 * rect.width() / 24.0,
                           3.0 * rect.height() / 24.0);
        painter.drawPath(mic);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(pen);
        QPainterPath arc;
        const QRectF arcRect(point(rect, 5, 9), point(rect, 19, 20));
        arc.moveTo(point(rect, 5, 13));
        arc.arcTo(arcRect, 180, 180);
        painter.drawPath(arc);
        line(12, 19, 12, 22);
        line(8, 22, 16, 22);
        if (icon == Icon::MicOff) {
            painter.setPen(QPen(color, qMax<qreal>(1.5, stroke + .2), Qt::SolidLine, Qt::RoundCap));
            line(4, 4, 20, 20);
        }
        break;
    }
    case Icon::Camera:
    case Icon::CameraOff: {
        QPainterPath path;
        path.addRoundedRect(QRectF(point(rect, 3, 6), point(rect, 16, 18)), 2 * rect.width() / 24.0,
                            2 * rect.height() / 24.0);
        painter.drawPath(path);
        drawPolyline(painter, rect, {{16, 10}, {21, 7}, {21, 17}, {16, 14}});
        if (icon == Icon::CameraOff) line(3, 3, 21, 21);
        break;
    }
    case Icon::Screen:
        rounded(2.5, 4, 19, 13, 2);
        line(9, 21, 15, 21);
        line(12, 17, 12, 21);
        break;
    case Icon::Settings:
        line(3, 7, 11, 7);
        line(16, 7, 21, 7);
        rounded(11, 4, 5, 5, 2.5);
        line(3, 17, 6, 17);
        line(11, 17, 21, 17);
        rounded(6, 14, 5, 5, 2.5);
        break;
    case Icon::Leave:
        drawPolyline(painter, rect, {{10, 4}, {4, 4}, {4, 20}, {10, 20}});
        drawPolyline(painter, rect, {{14, 8}, {20, 12}, {14, 16}});
        line(9, 12, 20, 12);
        break;
    case Icon::Users:
        painter.drawEllipse(point(rect, 8, 3), 3 * rect.width() / 24.0, 3 * rect.height() / 24.0);
        painter.drawArc(QRectF(point(rect, 3, 9), point(rect, 13, 21)), 0, 180 * 16);
        painter.drawEllipse(point(rect, 17, 5), 2.3 * rect.width() / 24.0, 2.3 * rect.height() / 24.0);
        painter.drawArc(QRectF(point(rect, 14, 11), point(rect, 22, 19)), 0, 110 * 16);
        break;
    case Icon::Headphones:
    case Icon::HeadphonesOff: {
        painter.drawArc(QRectF(point(rect, 3, 4), point(rect, 21, 22)), 20 * 16, 140 * 16);
        rounded(3, 11, 4, 8, 2);
        rounded(17, 11, 4, 8, 2);
        if (icon == Icon::HeadphonesOff) line(3, 3, 21, 21);
        break;
    }
    case Icon::Refresh:
        painter.drawArc(QRectF(point(rect, 4, 4), point(rect, 20, 20)), 35 * 16, 290 * 16);
        drawPolyline(painter, rect, {{18.5, 3.5}, {18.5, 9}, {13, 9}});
        break;
    case Icon::More:
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(point(rect, 5, 12), 1.5 * rect.width() / 24.0, 1.5 * rect.height() / 24.0);
        painter.drawEllipse(point(rect, 12, 12), 1.5 * rect.width() / 24.0, 1.5 * rect.height() / 24.0);
        painter.drawEllipse(point(rect, 19, 12), 1.5 * rect.width() / 24.0, 1.5 * rect.height() / 24.0);
        break;
    case Icon::Crown:
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        drawPolyline(painter, rect, {{3, 18}, {4, 8}, {9, 12}, {12, 5}, {15, 12}, {20, 8}, {21, 18}, {3, 18}});
        painter.drawPath([&]() {
            QPainterPath p(point(rect, 3, 18));
            p.lineTo(point(rect, 4, 8)); p.lineTo(point(rect, 9, 12)); p.lineTo(point(rect, 12, 5));
            p.lineTo(point(rect, 15, 12)); p.lineTo(point(rect, 20, 8)); p.lineTo(point(rect, 21, 18)); p.closeSubpath();
            return p;
        }());
        break;
    case Icon::Grid:
        rounded(3, 3, 7, 7, 1.5);
        rounded(14, 3, 7, 7, 1.5);
        rounded(3, 14, 7, 7, 1.5);
        rounded(14, 14, 7, 7, 1.5);
        break;
    case Icon::Focus:
        rounded(3, 4, 18, 11, 2);
        rounded(6, 17, 12, 4, 1);
        break;
    case Icon::Close:
        line(6, 6, 18, 18);
        line(18, 6, 6, 18);
        break;
    case Icon::Check:
        drawPolyline(painter, rect, {{4, 12.5}, {9.5, 18}, {20, 6}});
        break;
    case Icon::Alert:
        drawPolyline(painter, rect, {{12, 3}, {2.5, 20}, {21.5, 20}, {12, 3}});
        line(12, 9, 12, 14);
        painter.drawEllipse(point(rect, 12, 17), .7, .7);
        break;
    case Icon::Info:
        painter.drawEllipse(QRectF(point(rect, 3, 3), point(rect, 21, 21)));
        line(12, 11, 12, 17);
        painter.drawEllipse(point(rect, 12, 7.5), .7, .7);
        break;
    case Icon::Signal:
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        rounded(4, 15, 3, 5, .8);
        rounded(9, 12, 3, 8, .8);
        rounded(14, 9, 3, 11, .8);
        rounded(19, 5, 3, 15, .8);
        break;
    case Icon::Clock:
        painter.drawEllipse(QRectF(point(rect, 3, 3), point(rect, 21, 21)));
        line(12, 7, 12, 12);
        line(12, 12, 16, 14);
        break;
    case Icon::ChevronDown:
        drawPolyline(painter, rect, {{6, 9}, {12, 15}, {18, 9}});
        break;
    case Icon::Diagnostics:
        rounded(3, 4, 18, 16, 2);
        drawPolyline(painter, rect, {{7, 14}, {10, 14}, {12, 10}, {14, 16}, {16, 12}, {18, 12}});
        break;
    }
    painter.restore();
}

QPixmap pixmap(Icon icon, int size, const QColor &color, qreal stroke)
{
    const qreal ratio = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap result(qRound(size * ratio), qRound(size * ratio));
    result.setDevicePixelRatio(ratio);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    drawIcon(painter, icon, QRectF(0, 0, size, size), color, stroke);
    return result;
}

QFrame *card(QWidget *parent, const QString &objectName)
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(objectName);
    return frame;
}

QLabel *label(const QString &text, const QString &objectName, QWidget *parent)
{
    auto *result = new QLabel(text, parent);
    result->setObjectName(objectName);
    result->setWordWrap(objectName == QStringLiteral("Hint"));
    return result;
}

QPushButton *button(const QString &text, const QString &kind, QWidget *parent)
{
    auto *result = new QPushButton(text, parent);
    result->setProperty("kind", kind);
    result->setCursor(Qt::PointingHandCursor);
    return result;
}

QWidget *separator(QWidget *parent)
{
    auto *line = new QWidget(parent);
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background:#1f2933;"));
    return line;
}

QString avatarText(const QString &name)
{
    const QString trimmed = name.trimmed();
    return trimmed.isEmpty() ? QStringLiteral("?") : trimmed.left(1).toUpper();
}

QString formatDuration(int seconds)
{
    seconds = qMax(0, seconds);
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

} // namespace Theme