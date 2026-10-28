#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QMouseEvent>
#include <memory>

namespace muxplay {
enum class DecodeMode { Hardware = 0, Software = 1 };
class FramePump;
class IDisplayConsumer;
}

class GLPlayWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit GLPlayWidget(QWidget* parent = nullptr);
    ~GLPlayWidget() override;

    void setSoftwareFramePump(muxplay::FramePump* pump);
    void setDecodeMode(muxplay::DecodeMode mode);
    void requestNewFrame();
    void setKeepLastFrame(bool keep);
    void clearFrame();

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void recreateDisplay();

    muxplay::FramePump* softwareFramePump_ = nullptr;
    std::unique_ptr<muxplay::IDisplayConsumer> display_;
    muxplay::DecodeMode decodeMode_ = muxplay::DecodeMode::Software;
    bool glInited_ = false;
    bool keepLastFrame_ = true;
    bool forceClear_ = false;
};
