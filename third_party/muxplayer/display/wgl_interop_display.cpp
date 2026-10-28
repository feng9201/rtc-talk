#include "wgl_interop_display.h"
#include <d3d11.h>
#include <cstdio>
#include <cstring>

// ── GLSL 着色器（与 GLPlayWidget 相同）──
static const char* kVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
out vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

static const char* kFragmentShader = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 fragColor;
uniform sampler2D uTex;
void main() {
    fragColor = texture(uTex, vTexCoord);
}
)";

static const float kQuadVertices[] = {
    -1.0f, -1.0f,  0.0f, 1.0f,
     1.0f, -1.0f,  1.0f, 1.0f,
    -1.0f,  1.0f,  0.0f, 0.0f,
     1.0f,  1.0f,  1.0f, 0.0f,
};

namespace muxplay {

WGLInteropDisplay::WGLInteropDisplay(TextureProvider texProvider,
                                     D3DLockFn lock, D3DLockFn unlock)
    : texProvider_(std::move(texProvider))
    , d3dLock_(std::move(lock))
    , d3dUnlock_(std::move(unlock))
{
}

WGLInteropDisplay::~WGLInteropDisplay()
{
    glInterop_.shutdown();
    if (!gl_) return;
    if (glTexture_) { gl_->glDeleteTextures(1, &glTexture_); glTexture_ = 0; }
    if (program_)   { gl_->glDeleteProgram(program_);   program_ = 0; }
    if (vbo_)       { gl_->glDeleteBuffers(1, &vbo_);   vbo_ = 0; }
    if (vao_)       { gl_->glDeleteVertexArrays(1, &vao_); vao_ = 0; }
}

bool WGLInteropDisplay::initGL(QOpenGLFunctions_3_3_Core* gl)
{
    if (!gl) return false;
    gl_ = gl;
    if (!compileShaders()) {
        fprintf(stderr, "[WGLInteropDisplay] Shader compilation failed\n");
        return false;
    }
    createQuadGeometry();

    if (!glInterop_.init()) {
        fprintf(stderr, "[WGLInteropDisplay] WGL_NV_DX_interop NOT available\n");
        return false;
    }

    gl_->glGenTextures(1, &glTexture_);
    gl_->glBindTexture(GL_TEXTURE_2D, glTexture_);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glInited_ = true;
    return true;
}

bool WGLInteropDisplay::compileShaders()
{
    // Vertex shader
    GLuint vs = gl_->glCreateShader(GL_VERTEX_SHADER);
    gl_->glShaderSource(vs, 1, &kVertexShader, nullptr);
    gl_->glCompileShader(vs);
    GLint ok = 0;
    gl_->glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; gl_->glGetShaderInfoLog(vs, 512, nullptr, log);
        fprintf(stderr, "[WGL] VS compile error: %s\n", log);
        gl_->glDeleteShader(vs);
        return false;
    }

    // Fragment shader
    GLuint fs = gl_->glCreateShader(GL_FRAGMENT_SHADER);
    gl_->glShaderSource(fs, 1, &kFragmentShader, nullptr);
    gl_->glCompileShader(fs);
    gl_->glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; gl_->glGetShaderInfoLog(fs, 512, nullptr, log);
        fprintf(stderr, "[WGL] FS compile error: %s\n", log);
        gl_->glDeleteShader(vs); gl_->glDeleteShader(fs);
        return false;
    }

    // Link program
    program_ = gl_->glCreateProgram();
    gl_->glAttachShader(program_, vs);
    gl_->glAttachShader(program_, fs);
    gl_->glLinkProgram(program_);
    gl_->glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    gl_->glDeleteShader(vs);
    gl_->glDeleteShader(fs);
    if (!ok) {
        gl_->glDeleteProgram(program_);
        program_ = 0;
        return false;
    }
    return true;
}

void WGLInteropDisplay::createQuadGeometry()
{
    gl_->glGenVertexArrays(1, &vao_);
    gl_->glGenBuffers(1, &vbo_);

    gl_->glBindVertexArray(vao_);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl_->glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);

    gl_->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    gl_->glEnableVertexAttribArray(0);
    gl_->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    gl_->glEnableVertexAttribArray(1);

    gl_->glBindVertexArray(0);
}

void WGLInteropDisplay::renderQuad(int viewportW, int viewportH, int texW, int texH)
{
    if (!program_ || !vao_ || !glTexture_ || !gl_) return;
    if (viewportW <= 0 || viewportH <= 0 || texW <= 0 || texH <= 0) return;


    // // ── 宽高比自适应：居中 + letterbox/pillarbox ──
    // float texAspect  = (float)texW / (float)texH;
    // float viewAspect = (float)viewportW / (float)viewportH;
    // int drawW = viewportW;
    // int drawH = viewportH;
    // if (viewAspect > texAspect) {
    //     // 窗口比视频宽 → pillarbox（左右黑边）
    //     drawW = (int)(viewportH * texAspect);
    // } else {
    //     // 窗口比视频高 → letterbox（上下黑边）
    //     drawH = (int)(viewportW / texAspect);
    // }
    // int offsetX = (viewportW - drawW) / 2;
    // int offsetY = (viewportH - drawH) / 2;

    // 铺满整个窗口（不保持宽高比）
    gl_->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gl_->glClear(GL_COLOR_BUFFER_BIT);
    gl_->glViewport(0, 0, viewportW, viewportH);

    gl_->glUseProgram(program_);

    gl_->glActiveTexture(GL_TEXTURE0);
    gl_->glBindTexture(GL_TEXTURE_2D, glTexture_);
    GLint loc = gl_->glGetUniformLocation(program_, "uTex");
    if (loc >= 0) gl_->glUniform1i(loc, 0);

    gl_->glBindVertexArray(vao_);
    gl_->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl_->glBindVertexArray(0);
}

// ── IDisplayConsumer 实现 ──

void WGLInteropDisplay::paint(int viewportW, int viewportH)
{
    if (!glInited_ || !texProvider_ || !gl_) return;

    ID3D11Texture2D* sharedTex = texProvider_();
    if (!sharedTex) {
        gl_->glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
        gl_->glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    // WGL interop 操作需要 D3D11 ImmediateContext 同步锁
    d3dLock_();

    // 纹理变更时重新注册
    if (sharedTex != glInterop_.registeredTex()) {
        D3D11_TEXTURE2D_DESC desc = {};
        sharedTex->GetDesc(&desc);
        texWidth_  = (int)desc.Width;
        texHeight_ = (int)desc.Height;

        gl_->glBindTexture(GL_TEXTURE_2D, glTexture_);
        if (!glInterop_.registerTexture(sharedTex, glTexture_)) {
            fprintf(stderr, "[WGLInteropDisplay] registerTexture failed\n");
            d3dUnlock_();
            gl_->glClearColor(0.2f, 0.0f, 0.0f, 1.0f);
            gl_->glClear(GL_COLOR_BUFFER_BIT);
            return;
        }
    }

    // 锁定 WGL interop → 渲染 → 解锁
    if (glInterop_.lock()) {
        renderQuad(viewportW, viewportH, texWidth_, texHeight_);
        glInterop_.unlock();
    }

    d3dUnlock_();
}

void WGLInteropDisplay::clear()
{
    if (glInited_ && gl_) {
        gl_->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        gl_->glClear(GL_COLOR_BUFFER_BIT);
    }
}

void WGLInteropDisplay::notifyNewFrame()
{
    // 由 GLPlayWidget::requestNewFrame → QOpenGLWidget::update() 驱动重绘
    // 此方法本身不需要做任何操作
}

bool WGLInteropDisplay::isReady() const
{
    return glInited_;
}

} // namespace muxplay
