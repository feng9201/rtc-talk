#pragma once

#include <QImage>
#include <QWidget>

// Stable rendering boundary for the UI prototype.
// A production OpenGL/D3D or libwebrtc-backed widget only needs to inherit
// this class and deliver decoded frames through setFrame().
class VideoSurface : public QWidget
{
    Q_OBJECT

public:
    explicit VideoSurface(QWidget *parent = nullptr);
    ~VideoSurface() override;

    bool hasFrame() const { return !m_frame.isNull(); }
    QImage frame() const { return m_frame; }

public slots:
    virtual void setFrame(const QImage &frame);
    virtual void clearFrame();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_frame;
};