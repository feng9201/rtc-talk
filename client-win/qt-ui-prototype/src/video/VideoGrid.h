#pragma once

#include "app/UiState.h"

#include <QHash>
#include <QVector>
#include <QWidget>

class VideoTile;

class VideoGrid : public QWidget
{
    Q_OBJECT

public:
    explicit VideoGrid(QWidget *parent = nullptr);

    void setMembers(const QVector<MemberInfo> &members, const QString &selfId);
    void setSelectedId(const QString &id);
    void setLayoutMode(StageLayout layout);
    void setFilter(StageFilter filter);
    void setSharing(bool sharing);
    QString selectedId() const { return m_selectedId; }

signals:
    void memberSelected(const QString &id);
    void memberFocusRequested(const QString &id);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void syncTiles();
    void recalculate();
    QVector<MemberInfo> visibleMembers() const;
    VideoTile *tileFor(const QString &id) const;

    QVector<MemberInfo> m_members;
    QHash<QString, VideoTile *> m_tiles;
    VideoTile *m_shareTile = nullptr;
    QString m_selfId;
    QString m_selectedId;
    StageLayout m_layout = StageLayout::Grid;
    StageFilter m_filter = StageFilter::All;
    bool m_sharing = false;
};