#pragma once

#include "video/VideoSurface.h"

class MockVideoSurface : public VideoSurface
{
public:
    explicit MockVideoSurface(QWidget *parent = nullptr);

    void setMember(const QString &name, bool cameraEnabled, bool sharing, bool self, const QString &source);
    void setPreview(bool preview);
    void setCompact(bool compact);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QRgb accentColor() const;
    void paintSharePlaceholder(QPainter &painter);
    void paintAvatarPlaceholder(QPainter &painter);
    void paintCameraPlaceholder(QPainter &painter);

    QString m_name = QStringLiteral("我");
    bool m_cameraEnabled = false;
    bool m_sharing = false;
    bool m_self = true;
    bool m_preview = false;
    bool m_compact = false;
    QString m_source = QStringLiteral("mic");
};