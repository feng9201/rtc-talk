#include "video/VideoGrid.h"

#include "base/Theme.h"
#include "video/VideoTile.h"

#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>

VideoGrid::VideoGrid(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(300, 220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void VideoGrid::setMembers(const QVector<MemberInfo> &members, const QString &selfId)
{
    m_members = members;
    m_selfId = selfId;
    syncTiles();
    recalculate();
    update();
}

void VideoGrid::setSelectedId(const QString &id)
{
    m_selectedId = id;
    for (VideoTile *tile : m_tiles) tile->setSelected(tile->memberId() == id);
    recalculate();
}

void VideoGrid::setLayoutMode(StageLayout layout)
{
    if (m_layout == layout) return;
    m_layout = layout;
    recalculate();
}

void VideoGrid::setFilter(StageFilter filter)
{
    if (m_filter == filter) return;
    m_filter = filter;
    recalculate();
    update();
}

void VideoGrid::setSharing(bool sharing)
{
    if (m_sharing == sharing) return;
    m_sharing = sharing;
    syncTiles();
    recalculate();
    update();
}

VideoTile *VideoGrid::tileFor(const QString &id) const
{
    return m_tiles.value(id, nullptr);
}

void VideoGrid::syncTiles()
{
    QHash<QString, bool> used;
    for (const MemberInfo &member : m_members) {
        VideoTile *tile = m_tiles.value(member.id, nullptr);
        if (!tile) {
            tile = new VideoTile(this);
            m_tiles.insert(member.id, tile);
            connect(tile, &VideoTile::selected, this, &VideoGrid::memberSelected);
            connect(tile, &VideoTile::focusRequested, this, [this](const QString &id) {
                emit memberSelected(id);
                emit memberFocusRequested(id);
            });
        }
        const bool self = member.id == m_selfId || member.isSelf;
        tile->setMember(member, self, false, false);
        used.insert(member.id, true);
    }

    for (auto it = m_tiles.begin(); it != m_tiles.end();) {
        if (!used.contains(it.key())) {
            it.value()->deleteLater();
            it = m_tiles.erase(it);
        } else {
            ++it;
        }
    }

    if (m_sharing) {
        if (!m_shareTile) {
            m_shareTile = new VideoTile(this);
            m_shareTile->setCursor(Qt::ArrowCursor);
            m_shareTile->setPresentation(true);
        }
        MemberInfo screen;
        screen.id = QStringLiteral("__screen__");
        screen.name = QStringLiteral("我的屏幕共享");
        screen.camera = true;
        screen.source = QStringLiteral("screen");
        for (const MemberInfo &member : m_members) {
            if (member.id == m_selfId || member.isSelf) {
                screen.source = member.source;
                break;
            }
        }
        m_shareTile->setMember(screen, false, true, true);
        m_shareTile->show();
    } else if (m_shareTile) {
        m_shareTile->hide();
        m_shareTile->deleteLater();
        m_shareTile = nullptr;
    }
}

QVector<MemberInfo> VideoGrid::visibleMembers() const
{
    QVector<MemberInfo> result;
    for (const MemberInfo &member : m_members) {
        if (m_filter == StageFilter::VideoOnly && !member.camera) continue;
        result.append(member);
    }
    return result;
}

void VideoGrid::recalculate()
{
    if (width() <= 0 || height() <= 0) return;

    for (VideoTile *tile : m_tiles) tile->hide();
    if (m_shareTile) m_shareTile->hide();

    const QVector<MemberInfo> members = visibleMembers();
    const QRect content = rect().adjusted(1, 1, -1, -1);
    const int gap = 10;

    const auto place = [](VideoTile *tile, const QRect &rect, bool compact) {
        tile->setCompact(compact);
        tile->setGeometry(rect);
        tile->show();
    };

    const auto layoutFilmstrip = [&](const QVector<MemberInfo> &people, const QRect &strip) {
        if (people.isEmpty()) return;
        const int count = people.size();
        const int itemGap = 8;
        const int available = strip.width() - itemGap * (count - 1);
        const int itemWidth = std::clamp(available / qMax(1, count), 96, 172);
        for (int i = 0; i < count; ++i) {
            VideoTile *tile = tileFor(people.at(i).id);
            if (!tile) continue;
            tile->setSelected(people.at(i).id == m_selectedId);
            place(tile, QRect(strip.left() + i * (itemWidth + itemGap), strip.top(),
                              itemWidth, strip.height()), true);
        }
    };

    if (m_sharing) {
        if (!m_shareTile) return;
        const int stripHeight = std::clamp(qRound(content.height() * 0.15), 82, 104);
        const QRect mainRect(content.left(), content.top(), content.width(),
                             content.height() - stripHeight - gap);
        const QRect stripRect(content.left(), mainRect.bottom() + gap + 1,
                              content.width(), stripHeight);
        place(m_shareTile, mainRect, false);
        m_shareTile->raise();
        layoutFilmstrip(m_members, stripRect);
        update();
        return;
    }

    if (members.isEmpty()) {
        update();
        return;
    }

    for (const MemberInfo &member : members) {
        if (VideoTile *tile = tileFor(member.id)) tile->setSelected(member.id == m_selectedId);
    }

    if (m_members.size() == 2 && members.size() == 2 && m_layout == StageLayout::Grid) {
        VideoTile *remoteTile = nullptr;
        VideoTile *selfTile = nullptr;
        for (const MemberInfo &member : members) {
            if (member.id == m_selfId || member.isSelf) selfTile = tileFor(member.id);
            else remoteTile = tileFor(member.id);
        }
        if (remoteTile) {
            remoteTile->setCompact(false);
            remoteTile->setGeometry(content);
            remoteTile->show();
            remoteTile->raise();
        }
        if (selfTile) {
            const int pipWidth = std::clamp(qRound(content.width() * 0.24), 180, 270);
            const int pipHeight = qRound(pipWidth * 9.0 / 16.0);
            const int margin = 14;
            selfTile->setCompact(true);
            selfTile->setGeometry(QRect(content.right() - pipWidth - margin,
                                        content.bottom() - pipHeight - margin,
                                        pipWidth, pipHeight));
            selfTile->show();
            selfTile->raise();
        }
        update();
        return;
    }

    if (m_layout == StageLayout::Focus && members.size() > 1) {
        const MemberInfo *focusMember = nullptr;
        for (const MemberInfo &member : members) {
            if (member.id == m_selectedId) {
                focusMember = &member;
                break;
            }
        }
        if (!focusMember) focusMember = &members.first();

        VideoTile *focusTile = tileFor(focusMember->id);
        const int stripHeight = std::clamp(qRound(content.height() * 0.15), 82, 104);
        const QRect mainRect(content.left(), content.top(), content.width(),
                             content.height() - stripHeight - gap);
        const QRect stripRect(content.left(), mainRect.bottom() + gap + 1,
                              content.width(), stripHeight);
        if (focusTile) {
            place(focusTile, mainRect, false);
            focusTile->raise();
        }

        QVector<MemberInfo> thumbs;
        for (const MemberInfo &member : members) {
            if (member.id != focusMember->id) thumbs.append(member);
        }
        layoutFilmstrip(thumbs, stripRect);
        update();
        return;
    }

    int columns = 1;
    if (members.size() > 1 && members.size() <= 4) columns = 2;
    else if (members.size() > 4) columns = 3;
    const int rows = static_cast<int>(std::ceil(members.size() / static_cast<qreal>(columns)));
    const int tileWidth = (content.width() - gap * (columns - 1)) / columns;
    const int tileHeight = (content.height() - gap * (rows - 1)) / rows;
    const bool compact = members.size() > 4;
    for (int i = 0; i < members.size(); ++i) {
        VideoTile *tile = tileFor(members.at(i).id);
        if (!tile) continue;
        const int column = i % columns;
        const int row = i / columns;
        place(tile, QRect(content.left() + column * (tileWidth + gap),
                          content.top() + row * (tileHeight + gap),
                          tileWidth, tileHeight), compact);
    }
    update();
}

void VideoGrid::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    recalculate();
}

void VideoGrid::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    if (m_sharing || !visibleMembers().isEmpty()) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF box = QRectF(rect()).adjusted(12, 12, -12, -12);
    QPainterPath path;
    path.addRoundedRect(box, 14, 14);
    painter.setPen(QPen(QColor(QStringLiteral("#2d3d4d")), 1, Qt::DashLine));
    painter.setBrush(QColor(15, 20, 26, 115));
    painter.drawPath(path);

    Theme::drawIcon(painter, Theme::Icon::CameraOff,
                    QRectF(width() / 2.0 - 20, height() / 2.0 - 70, 40, 40),
                    QColor(QStringLiteral("#5c6b79")), 1.6);
    QFont titleFont = painter.font();
    titleFont.setPixelSize(15);
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(QColor(QStringLiteral("#e8edf2")));
    painter.drawText(QRect(0, height() / 2 - 20, width(), 28), Qt::AlignCenter,
                     m_members.isEmpty() ? QStringLiteral("等待成员加入")
                                         : QStringLiteral("暂无视频成员"));
    QFont hintFont = painter.font();
    hintFont.setPixelSize(12);
    hintFont.setWeight(QFont::Normal);
    painter.setFont(hintFont);
    painter.setPen(QColor(QStringLiteral("#8a9aa8")));
    painter.drawText(QRect(0, height() / 2 + 10, width(), 48), Qt::AlignHCenter | Qt::TextWordWrap,
                     m_members.isEmpty() ? QStringLiteral("有成员加入后将在这里显示画面")
                                         : QStringLiteral("当前为音频模式，可切回“全部成员”查看头像"));
}
