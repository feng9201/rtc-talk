#include "soft_gl_display.h"
#include "gl_yuv_shader.h"
#include <cstdio>

extern "C" {
#include <libavutil/frame.h>
}

namespace muxplay {

SoftGLDisplay::SoftGLDisplay(FrameProvider frameProvider)
    : frameProvider_(std::move(frameProvider))
{
}

SoftGLDisplay::~SoftGLDisplay()
{
    yuvShader_.reset();
}

bool SoftGLDisplay::initGL(QOpenGLFunctions_3_3_Core* gl)
{
    if (!gl) return false;
    gl_ = gl;
    yuvShader_ = std::make_unique<GLYuvShader>();
    if (!yuvShader_->init(gl_)) {
        fprintf(stderr, "[SoftGLDisplay] YUV shader init failed\n");
        return false;
    }
    glInited_ = true;
    return true;
}

void SoftGLDisplay::paint(int viewportW, int viewportH)
{
    if (!glInited_ || !yuvShader_ || !frameProvider_ || !gl_) return;

    // 1. 获取最新解码帧
    AVFrame* frame = frameProvider_();
    if (!frame) {
        // 无帧可用：黑屏
        gl_->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        gl_->glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    // 2. 上传 YUV → GL 纹理
    yuvShader_->uploadFrame(frame);

    // 3. 渲染 YUV→RGB quad
    yuvShader_->render(viewportW, viewportH);

    // 4. 释放帧引用
    av_frame_free(&frame);
}

void SoftGLDisplay::clear()
{
    if (glInited_ && gl_) {
        gl_->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        gl_->glClear(GL_COLOR_BUFFER_BIT);
    }
}

void SoftGLDisplay::notifyNewFrame()
{
    // 由 GLPlayWidget::requestNewFrame → QOpenGLWidget::update() 驱动
}

bool SoftGLDisplay::isReady() const
{
    return glInited_;
}

} // namespace muxplay
