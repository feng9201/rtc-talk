#include "video/VideoGrid.h"

#include "video/VideoTile.h"

#include <QPainter>
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
}

void VideoGrid::setSharing(bool sharing)
{
    if (m_sharing == sharing) return;
    m_sharing = sharing;
    syncTiles();
    recalculate();
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
        tile->setMember(member, self, m_sharing, m_sharing && self);
        tile->show();
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
}

QVector<MemberInfo> VideoGrid::visibleMembers() const
{
    QVector<MemberInfo> result;
    for (const MemberInfo &member : m_members) {
        if (m_filter == StageFilter::VideoOnly && !member.camera) continue;
        result.append(member);
    }
    if (result.isEmpty() && !m_members.isEmpty()) result.append(m_members.first());
    return result;
}

void VideoGrid::recalculate()
{
    if (width() <= 0 || height() <= 0) return;
    for (VideoTile *tile : m_tiles) tile->hide();

    const QVector<MemberInfo> members = visibleMembers();
    if (members.isEmpty()) return;

    for (const MemberInfo &member : members) {
        VideoTile *tile = tileFor(member.id);
        if (!tile) continue;
        tile->show();
        tile->setSelected(member.id == m_selectedId);
    }

    const int gap = 12;
    const QRect content = rect().adjusted(0, 0, -1, -1);

    const auto place = [gap](VideoTile *tile, const QRect &r, bool compact) {
        tile->setCompact(compact);
        tile->setGeometry(r);
    };

    if (m_sharing) {
        const int sideWidth = std::clamp(qRound(content.width() * 0.255), 220, 350);
        QRect big(content.left(), content.top(), content.width() - sideWidth - gap, content.height());
        VideoTile *share = tileFor(m_selfId);
        if (!share) share = tileFor(members.first().id);
        if (share) {
            share->setCompact(false);
            share->setGeometry(big);
            share->show();
            share->raise();
        }

        QVector<VideoTile *> thumbs;
        for (const MemberInfo &member : members) {
            if (share && member.id == share->memberId()) continue;
            if (VideoTile *tile = tileFor(member.id)) thumbs.append(tile);
        }
        const int sideX = big.right() + gap + 1;
        const int cols = sideWidth >= 220 ? 2 : 1;
        const int rows = qMax(1, static_cast<int>(std::ceil(thumbs.size() / static_cast<qreal>(cols))));
        const int thumbGap = 8;
        const int tw = (sideWidth - thumbGap * (cols - 1)) / cols;
        const int th = (content.height() - thumbGap * (rows - 1)) / qMax(1, rows);
        for (int i = 0; i < thumbs.size(); ++i) {
            const int col = i % cols;
            const int row = i / cols;
            place(thumbs[i], QRect(sideX + col * (tw + thumbGap), content.top() + row * (th + thumbGap), tw, th), true);
        }
        return;
    }

    if (members.size() == 2 && m_layout == StageLayout::Grid) {
        const MemberInfo *remote = nullptr;
        VideoTile *remoteTile = nullptr;
        VideoTile *selfTile = nullptr;
        for (const MemberInfo &member : members) {
            if (member.id == m_selfId || member.isSelf) selfTile = tileFor(member.id);
            else {
                remote = &member;
                remoteTile = tileFor(member.id);
            }
        }
        if (remoteTile) {
            remoteTile->setCompact(false);
            remoteTile->setGeometry(content);
            remoteTile->show();
            remoteTile->raise();
        }
        if (selfTile) {
            const int pipW = std::clamp(qRound(content.width() * 0.27), 250, 390);
            const int pipH = std::clamp(qRound(content.height() * 0.38), 170, 310);
            const int margin = 20;
            selfTile->setCompact(true);
            selfTile->setGeometry(QRect(content.right() - pipW - margin,
                                       content.bottom() - pipH - margin, pipW, pipH));
            selfTile->show();
            selfTile->raise();
        }
        Q_UNUSED(remote)
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
        const int sideWidth = std::clamp(qRound(content.width() * 0.27), 240, 370);
        const QRect big(content.left(), content.top(), content.width() - sideWidth - gap, content.height());
        if (focusTile) {
            focusTile->setCompact(false);
            focusTile->setGeometry(big);
            focusTile->raise();
        }

        QVector<VideoTile *> thumbs;
        for (const MemberInfo &member : members) {
            if (member.id == focusMember->id) continue;
            if (VideoTile *tile = tileFor(member.id)) thumbs.append(tile);
        }
        const int sideX = big.right() + gap + 1;
        const int cols = sideWidth >= 260 ? 2 : 1;
        const int rows = qMax(1, static_cast<int>(std::ceil(thumbs.size() / static_cast<qreal>(cols))));
        const int thumbGap = 8;
        const int tw = (sideWidth - thumbGap * (cols - 1)) / cols;
        const int th = (content.height() - thumbGap * (rows - 1)) / qMax(1, rows);
        for (int i = 0; i < thumbs.size(); ++i) {
            const int col = i % cols;
            const int row = i / cols;
            place(thumbs[i], QRect(sideX + col * (tw + thumbGap), content.top() + row * (th + thumbGap), tw, th), true);
        }
        return;
    }

    int cols = 1;
    if (members.size() >= 7) cols = 3;
    else if (members.size() >= 3) cols = members.size() == 4 ? 2 : 3;
    else if (members.size() == 2) cols = 2;
    const int rows = static_cast<int>(std::ceil(members.size() / static_cast<qreal>(cols)));
    const int tw = (content.width() - gap * (cols - 1)) / cols;
    const int th = (content.height() - gap * (rows - 1)) / rows;
    for (int i = 0; i < members.size(); ++i) {
        VideoTile *tile = tileFor(members[i].id);
        if (!tile) continue;
        const int col = i % cols;
        const int row = i / cols;
        place(tile, QRect(content.left() + col * (tw + gap), content.top() + row * (th + gap), tw, th), members.size() > 4);
    }
}

void VideoGrid::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    recalculate();
}

void VideoGrid::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    if (!m_tiles.isEmpty()) return;
    QPainter painter(this);
    painter.setPen(QColor(QStringLiteral("#5c6b79")));
    painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("暂无成员画面"));
}