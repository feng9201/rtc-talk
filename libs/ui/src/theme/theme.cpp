#include "rtcwin/ui/theme/theme.h"

#include <QApplication>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QStyleFactory>

static void InitRtcwinResources() {
  Q_INIT_RESOURCE(resources);
}
namespace rtcwin::ui::theme {
namespace {

QPointF P(const QRectF& rect, qreal x, qreal y) {
  return QPointF(rect.left() + x * rect.width() / 24.0,
                 rect.top() + y * rect.height() / 24.0);
}

void DrawLine(QPainter& painter, const QRectF& rect, qreal x1, qreal y1,
              qreal x2, qreal y2) {
  painter.drawLine(P(rect, x1, y1), P(rect, x2, y2));
}

QPainterPath Polyline(const QRectF& rect,
                      std::initializer_list<QPointF> points) {
  auto it = points.begin();
  QPainterPath path(P(rect, it->x(), it->y()));
  ++it;
  for (; it != points.end(); ++it) path.lineTo(P(rect, it->x(), it->y()));
  return path;
}

}  // namespace

void Apply(QApplication& app) {
  InitRtcwinResources();
  app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
  QPalette palette;
  palette.setColor(QPalette::Window, QColor(QStringLiteral("#0b0d10")));
  palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#e8edf2")));
  palette.setColor(QPalette::Base, QColor(QStringLiteral("#0c1218")));
  palette.setColor(QPalette::AlternateBase,
                   QColor(QStringLiteral("#11171d")));
  palette.setColor(QPalette::Text, QColor(QStringLiteral("#e8edf2")));
  palette.setColor(QPalette::Button, QColor(QStringLiteral("#151e27")));
  palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#e8edf2")));
  palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#55c2ff")));
  palette.setColor(QPalette::HighlightedText,
                   QColor(QStringLiteral("#04141c")));
  palette.setColor(QPalette::PlaceholderText,
                   QColor(QStringLiteral("#5c6b79")));
  app.setPalette(palette);

  QFile file(QStringLiteral(":/rtcwin/theme.qss"));
  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    app.setStyleSheet(QString::fromUtf8(file.readAll()));
  }
}

QPixmap Pixmap(Icon icon, int size, const QColor& color) {
  const qreal ratio = qApp != nullptr ? qApp->devicePixelRatio() : 1.0;
  QPixmap result(qRound(size * ratio), qRound(size * ratio));
  result.setDevicePixelRatio(ratio);
  result.fill(Qt::transparent);
  QPainter painter(&result);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap,
                      Qt::RoundJoin));
  const QRectF r(0, 0, size, size);

  switch (icon) {
    case Icon::Mic:
    case Icon::MicOff: {
      painter.setBrush(color);
      painter.setPen(Qt::NoPen);
      painter.drawRoundedRect(QRectF(P(r, 9, 3), P(r, 15, 15)),
                              3.0 * size / 24.0, 3.0 * size / 24.0);
      painter.setBrush(Qt::NoBrush);
      painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap,
                          Qt::RoundJoin));
      painter.drawArc(QRectF(P(r, 5, 9), P(r, 19, 21)), 180 * 16, 180 * 16);
      DrawLine(painter, r, 12, 19, 12, 22);
      DrawLine(painter, r, 8, 22, 16, 22);
      if (icon == Icon::MicOff) DrawLine(painter, r, 4, 4, 20, 20);
      break;
    }
    case Icon::Camera:
    case Icon::CameraOff: {
      painter.drawRoundedRect(QRectF(P(r, 3, 6), P(r, 16, 18)),
                              2 * size / 24.0, 2 * size / 24.0);
      painter.drawPath(Polyline(r, {P(r, 16, 10), P(r, 21, 7), P(r, 21, 17),
                                    P(r, 16, 14)}));
      if (icon == Icon::CameraOff) DrawLine(painter, r, 3, 3, 21, 21);
      break;
    }
    case Icon::Screen:
      painter.drawRoundedRect(QRectF(P(r, 3, 4), P(r, 21, 17)), 3, 3);
      DrawLine(painter, r, 9, 21, 15, 21);
      DrawLine(painter, r, 12, 17, 12, 21);
      break;
    case Icon::Settings:
      DrawLine(painter, r, 3, 7, 11, 7);
      DrawLine(painter, r, 16, 7, 21, 7);
      painter.drawEllipse(P(r, 12, 7), 2.2 * size / 24.0,
                          2.2 * size / 24.0);
      DrawLine(painter, r, 3, 17, 6, 17);
      DrawLine(painter, r, 11, 17, 21, 17);
      painter.drawEllipse(P(r, 8, 17), 2.2 * size / 24.0,
                          2.2 * size / 24.0);
      break;
    case Icon::Leave:
      painter.drawPath(Polyline(r, {P(r, 10, 4), P(r, 4, 4), P(r, 4, 20),
                                    P(r, 10, 20)}));
      painter.drawPath(Polyline(r, {P(r, 14, 8), P(r, 20, 12),
                                    P(r, 14, 16)}));
      DrawLine(painter, r, 9, 12, 20, 12);
      break;
    case Icon::Users:
      painter.drawEllipse(P(r, 8, 4), 3 * size / 24.0, 3 * size / 24.0);
      painter.drawArc(QRectF(P(r, 3, 9), P(r, 13, 21)), 0, 180 * 16);
      painter.drawEllipse(P(r, 17, 6), 2.2 * size / 24.0,
                          2.2 * size / 24.0);
      painter.drawArc(QRectF(P(r, 14, 11), P(r, 22, 20)), 0, 110 * 16);
      break;
    case Icon::Headphones:
    case Icon::HeadphonesOff:
      painter.drawArc(QRectF(P(r, 3, 4), P(r, 21, 22)), 20 * 16, 140 * 16);
      painter.drawRoundedRect(QRectF(P(r, 3, 11), P(r, 7, 19)), 2, 2);
      painter.drawRoundedRect(QRectF(P(r, 17, 11), P(r, 21, 19)), 2, 2);
      if (icon == Icon::HeadphonesOff) DrawLine(painter, r, 3, 3, 21, 21);
      break;
    case Icon::Grid:
      painter.drawRoundedRect(QRectF(P(r, 3, 3), P(r, 10, 10)), 2, 2);
      painter.drawRoundedRect(QRectF(P(r, 14, 3), P(r, 21, 10)), 2, 2);
      painter.drawRoundedRect(QRectF(P(r, 3, 14), P(r, 10, 21)), 2, 2);
      painter.drawRoundedRect(QRectF(P(r, 14, 14), P(r, 21, 21)), 2, 2);
      break;
    case Icon::Focus:
      painter.drawRoundedRect(QRectF(P(r, 3, 4), P(r, 21, 16)), 2, 2);
      painter.drawRoundedRect(QRectF(P(r, 7, 17), P(r, 17, 21)), 1, 1);
      break;
    case Icon::Close:
      DrawLine(painter, r, 6, 6, 18, 18);
      DrawLine(painter, r, 18, 6, 6, 18);
      break;
    case Icon::Maximize:
      painter.drawRect(QRectF(P(r, 5, 5), P(r, 19, 19)));
      break;
    case Icon::Minimize:
      DrawLine(painter, r, 5, 17, 19, 17);
      break;
    case Icon::Crown:
      painter.setBrush(color);
      painter.setPen(Qt::NoPen);
      painter.drawPath(
          Polyline(r, {P(r, 3, 18), P(r, 4, 8), P(r, 9, 12), P(r, 12, 5),
                       P(r, 15, 12), P(r, 20, 8), P(r, 21, 18),
                       P(r, 3, 18)}));
      break;
    case Icon::Clock:
      painter.drawEllipse(QRectF(P(r, 3, 3), P(r, 21, 21)));
      DrawLine(painter, r, 12, 7, 12, 12);
      DrawLine(painter, r, 12, 12, 16, 14);
      break;
    case Icon::Refresh:
      painter.drawArc(QRectF(P(r, 4, 4), P(r, 20, 20)), 35 * 16, 290 * 16);
      painter.drawPath(
          Polyline(r, {P(r, 18, 4), P(r, 18.5, 9), P(r, 13, 9)}));
      break;
    case Icon::More:
      painter.setBrush(color);
      painter.setPen(Qt::NoPen);
      painter.drawEllipse(P(r, 5, 12), 1.4, 1.4);
      painter.drawEllipse(P(r, 12, 12), 1.4, 1.4);
      painter.drawEllipse(P(r, 19, 12), 1.4, 1.4);
      break;
  }
  return result;
}

QFrame* Card(QWidget* parent, const QString& object_name) {
  auto* card = new QFrame(parent);
  card->setObjectName(object_name);
  card->setAttribute(Qt::WA_StyledBackground, true);

  return card;
}

QLabel* Label(const QString& text, const QString& object_name,
              QWidget* parent) {
  auto* label = new QLabel(text, parent);
  label->setObjectName(object_name);
  label->setWordWrap(object_name == QStringLiteral("Hint"));
  return label;
}

QPushButton* Button(const QString& text, const QString& kind,
                    QWidget* parent) {
  auto* button = new QPushButton(text, parent);
  button->setProperty("kind", kind);
  button->setCursor(Qt::PointingHandCursor);
  return button;
}

QString AvatarText(const QString& name) {
  const QString trimmed = name.trimmed();
  return trimmed.isEmpty() ? QStringLiteral("?")
                           : trimmed.left(1).toUpper();
}

QString FormatDuration(qint64 seconds) {
  seconds = qMax<qint64>(0, seconds);
  return QStringLiteral("%1:%2:%3")
      .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
      .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
      .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

}  // namespace rtcwin::ui::theme
