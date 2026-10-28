#pragma once

#include <QOpenGLFunctions_3_3_Core>

struct AVFrame;

namespace muxplay {

// ── YUV→RGB OpenGL 着色器 ──
// 封装 YUV 平面纹理上传和 YUV→RGB 转换渲染。
// 支持 YUV420P 格式（Y/U/V 三个独立平面）。
// 自动选择 BT.601 / BT.709 色彩矩阵。
class GLYuvShader {
public:
    GLYuvShader();
    ~GLYuvShader();

    // 初始化 GL 资源（编译 shader，创建纹理，创建 quad 几何）
    // gl: QOpenGLFunctions_3_3_Core 指针（GL context 已激活）
    bool init(QOpenGLFunctions_3_3_Core* gl);

    // 上传 YUV 帧数据到 GL 纹理
    // frame: AV_PIX_FMT_YUV420P 格式的 AVFrame
    void uploadFrame(const AVFrame* frame);

    // 渲染 YUV→RGB quad
    void render(int viewportW, int viewportH);

    bool isReady() const { return ready_; }

private:
    bool compileShader();
    void createQuadGeometry();

    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    bool ready_ = false;

    GLuint program_ = 0;
    GLuint vao_     = 0;
    GLuint vbo_     = 0;

    // Y/U/V 三个 GL 纹理
    GLuint texY_ = 0;
    GLuint texU_ = 0;
    GLuint texV_ = 0;

    // 当前帧尺寸
    int yWidth_  = 0;
    int yHeight_ = 0;
    int uWidth_  = 0;
    int uHeight_ = 0;
    int vWidth_  = 0;
    int vHeight_ = 0;
};

} // namespace muxplay
