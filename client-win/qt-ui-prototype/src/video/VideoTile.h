#pragma once

#include "app/UiState.h"

#include <QWidget>

class QLabel;
class MockVideoSurface;

class VideoTile : public QWidget
{
    Q_OBJECT

public:
    explicit VideoTile(QWidget *parent = nullptr);

    void setMember(const MemberInfo &member, bool self, bool sharing, bool activeShare);
    void setSelected(bool selected);
    QString memberId() const { return m_memberId; }
    bool isSelf() const { return m_self; }
    void setCompact(bool compact);
    void setPresentation(bool presentation);

signals:
    void selected(const QString &id);
    void focusRequested(const QString &id);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void rebuildStatusIcons();
    void positionOverlays();

    QString m_memberId;
    MemberInfo m_member;
    bool m_self = false;
    bool m_sharing = false;
    bool m_activeShare = false;
    bool m_selected = false;
    bool m_compact = false;
    bool m_presentation = false;
    bool m_hovered = false;

    MockVideoSurface *m_surface = nullptr;
    QLabel *m_name = nullptr;
    QLabel *m_role = nullptr;
    QLabel *m_primaryBadge = nullptr;
    QLabel *m_secondaryBadge = nullptr;
    QLabel *m_mic = nullptr;
    QLabel *m_camera = nullptr;
    QLabel *m_crown = nullptr;
};