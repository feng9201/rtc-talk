#include "video/VideoTile.h"

#include "base/Theme.h"
#include "video/MockVideoSurface.h"

#include <QEnterEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

VideoTile::VideoTile(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setMinimumSize(160, 110);

    m_surface = new MockVideoSurface(this);

    const auto makeLabel = [this](const QString &name) {
        auto *label = new QLabel(this);
        label->setObjectName(name);
        label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        label->setStyleSheet(QStringLiteral("background:transparent;"));
        return label;
    };
    m_name = makeLabel(QStringLiteral("TileName"));
    m_name->setStyleSheet(QStringLiteral(
        "background:rgba(5,10,15,210); color:#eef4f8; border:1px solid rgba(89,116,137,160);"
        "border-radius:6px; padding:3px 8px; font-weight:600;"));
    m_role = makeLabel(QStringLiteral("TileRole"));
    m_role->setStyleSheet(QStringLiteral("color:#dce7ee; background:rgba(9,15,21,185); padding:2px 6px; border-radius:4px;"));
    m_role->hide();

    m_primaryBadge = makeLabel(QStringLiteral("TileBadge"));
    m_primaryBadge->setStyleSheet(QStringLiteral(
        "color:#ffca71; background:rgba(50,36,18,225); border:1px solid #76501d;"
        "border-radius:5px; padding:2px 6px; font-size:11px;"));
    m_secondaryBadge = makeLabel(QStringLiteral("TileBadge"));
    m_secondaryBadge->setStyleSheet(QStringLiteral(
        "color:#6bd0ff; background:rgba(10,42,58,225); border:1px solid #1d617c;"
        "border-radius:5px; padding:2px 6px; font-size:11px;"));

    m_mic = makeLabel(QStringLiteral("TileStatus"));
    m_camera = makeLabel(QStringLiteral("TileStatus"));
    m_crown = makeLabel(QStringLiteral("TileCrown"));
    m_crown->setPixmap(Theme::pixmap(Theme::Icon::Crown, 14, QColor(QStringLiteral("#ffcf73"))));

    m_name->hide();
    m_primaryBadge->hide();
    m_secondaryBadge->hide();
    m_mic->hide();
    m_camera->hide();
    m_crown->hide();
}

void VideoTile::setMember(const MemberInfo &member, bool self, bool sharing, bool activeShare)
{
    const bool identityChanged = m_memberId != member.id;
    m_member = member;
    m_memberId = member.id;
    m_self = self;
    m_sharing = sharing;
    m_activeShare = activeShare;
    m_surface->setMember(member.name, member.camera, sharing, self, member.source);
    m_surface->setCompact(m_compact);

    QString title = member.name;
    if (self) title = member.name == QStringLiteral("我") ? QStringLiteral("我 (我)") : QStringLiteral("%1 (我)").arg(member.name);
    m_name->setText(title);
    m_name->adjustSize();
    m_name->show();

    m_crown->setVisible(member.role == QStringLiteral("host"));
    rebuildStatusIcons();
    positionOverlays();
    if (identityChanged) update();
}

void VideoTile::setSelected(bool selected)
{
    if (m_selected == selected) return;
    m_selected = selected;
    update();
}

void VideoTile::setCompact(bool compact)
{
    m_compact = compact;
    m_surface->setCompact(compact);
    positionOverlays();
}

void VideoTile::rebuildStatusIcons()
{
    const QColor muted(QStringLiteral("#ff7b7b"));
    const QColor normal(QStringLiteral("#d5e0e7"));
    m_mic->setPixmap(Theme::pixmap(m_member.mic ? Theme::Icon::Mic : Theme::Icon::MicOff, m_compact ? 14 : 16,
                                   m_member.mic ? normal : muted));
    m_camera->setPixmap(Theme::pixmap(m_member.camera ? Theme::Icon::Camera : Theme::Icon::CameraOff,
                                     m_compact ? 14 : 16, m_member.camera ? normal : muted));
    m_mic->show();
    m_camera->show();

    QString primary;
    QString secondary;
    if (m_member.poor) primary = QStringLiteral("网络差");
    if (m_member.source == QStringLiteral("system")) secondary = QStringLiteral("系统音");
    else if (m_sharing) secondary = QStringLiteral("共享中");
    if (!m_self && m_member.role == QStringLiteral("host")) {
        if (primary.isEmpty()) primary = QStringLiteral("主持人");
        else secondary = QStringLiteral("主持人");
    }
    if (m_activeShare && m_self) secondary = QStringLiteral("共享中");

    m_primaryBadge->setText(primary);
    m_primaryBadge->setVisible(!primary.isEmpty());
    m_primaryBadge->adjustSize();
    m_secondaryBadge->setText(secondary);
    m_secondaryBadge->setVisible(!secondary.isEmpty());
    m_secondaryBadge->adjustSize();
}

void VideoTile::positionOverlays()
{
    const int margin = m_compact ? 8 : 12;
    m_name->move(margin, height() - m_name->height() - margin);

    int top = margin;
    if (m_primaryBadge->isVisible()) {
        m_primaryBadge->move(margin, top);
        top += m_primaryBadge->height() + 5;
    }
    if (m_secondaryBadge->isVisible()) m_secondaryBadge->move(margin, top);

    int right = width() - margin;
    if (m_camera->isVisible()) {
        right -= m_camera->width();
        m_camera->move(right, height() - m_camera->height() - margin);
        right -= 6;
    }
    if (m_mic->isVisible()) {
        right -= m_mic->width();
        m_mic->move(right, height() - m_mic->height() - margin);
        right -= 6;
    }
    if (m_crown->isVisible()) {
        m_crown->move(m_name->geometry().right() + 7, m_name->y() + (m_name->height() - m_crown->height()) / 2);
    }

    m_surface->setGeometry(rect().adjusted(1, 1, -1, -1));
    m_surface->lower();
}

void VideoTile::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 13, 13);
    painter.fillPath(path, QColor(QStringLiteral("#10171d")));
    QColor border(QStringLiteral("#22313e"));
    if (m_hovered) border = QColor(QStringLiteral("#355165"));
    if (m_selected) border = QColor(QStringLiteral("#43d9a5"));
    painter.setPen(QPen(border, m_selected ? 2.0 : 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
}

void VideoTile::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    positionOverlays();
}

void VideoTile::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) emit selected(m_memberId);
    QWidget::mousePressEvent(event);
}

void VideoTile::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) emit focusRequested(m_memberId);
    QWidget::mouseDoubleClickEvent(event);
}

void VideoTile::enterEvent(QEvent *event)
{
    QWidget::enterEvent(event);
    m_hovered = true;
    update();
}

void VideoTile::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    m_hovered = false;
    update();
}