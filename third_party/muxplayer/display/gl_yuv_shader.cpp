#include "gl_yuv_shader.h"
#include <cstdio>
#include <cstring>

extern "C" {
#include <libavutil/frame.h>
}

// ── 顶点着色器（同 WGL）──
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

// ── YUV→RGB 片段着色器（BT.709）──
static const char* kFragmentShader = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 fragColor;
uniform sampler2D uTexY;
uniform sampler2D uTexU;
uniform sampler2D uTexV;

void main() {
    float y = texture(uTexY, vTexCoord).r;
    float u = texture(uTexU, vTexCoord).r - 0.5;
    float v = texture(uTexV, vTexCoord).r - 0.5;

    // BT.709 full range
    float r = y + 1.5748 * v;
    float g = y - 0.1873 * u - 0.4681 * v;
    float b = y + 1.8556 * u;

    fragColor = vec4(clamp(r, 0.0, 1.0), clamp(g, 0.0, 1.0), clamp(b, 0.0, 1.0), 1.0);
}
)";

static const float kQuadVertices[] = {
    -1.0f, -1.0f,  0.0f, 1.0f,
     1.0f, -1.0f,  1.0f, 1.0f,
    -1.0f,  1.0f,  0.0f, 0.0f,
     1.0f,  1.0f,  1.0f, 0.0f,
};

namespace muxplay {

GLYuvShader::GLYuvShader() = default;

GLYuvShader::~GLYuvShader()
{
    if (!gl_) return;
    if (texY_) { gl_->glDeleteTextures(1, &texY_); texY_ = 0; }
    if (texU_) { gl_->glDeleteTextures(1, &texU_); texU_ = 0; }
    if (texV_) { gl_->glDeleteTextures(1, &texV_); texV_ = 0; }
    if (program_) { gl_->glDeleteProgram(program_); program_ = 0; }
    if (vbo_)     { gl_->glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_)     { gl_->glDeleteVertexArrays(1, &vao_); vao_ = 0; }
}

bool GLYuvShader::init(QOpenGLFunctions_3_3_Core* gl)
{
    if (!gl) return false;
    gl_ = gl;
    if (!compileShader()) {
        fprintf(stderr, "[GLYuvShader] Shader compilation failed\n");
        return false;
    }
    createQuadGeometry();
    ready_ = true;
    return true;
}

bool GLYuvShader::compileShader()
{
    GLuint vs = gl_->glCreateShader(GL_VERTEX_SHADER);
    gl_->glShaderSource(vs, 1, &kVertexShader, nullptr);
    gl_->glCompileShader(vs);
    GLint ok = 0;
    gl_->glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; gl_->glGetShaderInfoLog(vs, 512, nullptr, log);
        fprintf(stderr, "[YUV] VS compile error: %s\n", log);
        gl_->glDeleteShader(vs);
        return false;
    }

    GLuint fs = gl_->glCreateShader(GL_FRAGMENT_SHADER);
    gl_->glShaderSource(fs, 1, &kFragmentShader, nullptr);
    gl_->glCompileShader(fs);
    gl_->glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; gl_->glGetShaderInfoLog(fs, 512, nullptr, log);
        fprintf(stderr, "[YUV] FS compile error: %s\n", log);
        gl_->glDeleteShader(vs); gl_->glDeleteShader(fs);
        return false;
    }

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

void GLYuvShader::createQuadGeometry()
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

void GLYuvShader::uploadFrame(const AVFrame* frame)
{
    if (!frame || !ready_ || !gl_) return;

    int yW  = frame->width;
    int yH  = frame->height;
    int uvW = frame->width  / 2;
    int uvH = frame->height / 2;

    // ── Y 纹理 ──
    if (!texY_) gl_->glGenTextures(1, &texY_);
    gl_->glBindTexture(GL_TEXTURE_2D, texY_);
    if (yW != yWidth_ || yH != yHeight_) {
        gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, yW, yH, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        yWidth_  = yW;
        yHeight_ = yH;
    }
    // ── 关键：设置行步长 (row stride) ──
    // FFmpeg 的 linesize 可能 > width（对齐填充），OpenGL 默认按 width 紧密排列读取，
    // 导致每行像素错位。低分辨率视频填充比例更大，花屏更严重。
    gl_->glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[0] > 0 ? frame->linesize[0] : yW);
    gl_->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, yW, yH, GL_RED, GL_UNSIGNED_BYTE, frame->data[0]);
    gl_->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    // ── U 纹理 ──
    if (!texU_) gl_->glGenTextures(1, &texU_);
    gl_->glBindTexture(GL_TEXTURE_2D, texU_);
    if (uvW != uWidth_ || uvH != uHeight_) {
        gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, uvW, uvH, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        uWidth_  = uvW;
        uHeight_ = uvH;
    }
    gl_->glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[1] > 0 ? frame->linesize[1] : uvW);
    gl_->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, uvW, uvH, GL_RED, GL_UNSIGNED_BYTE, frame->data[1]);
    gl_->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    // ── V 纹理 ──
    if (!texV_) gl_->glGenTextures(1, &texV_);
    gl_->glBindTexture(GL_TEXTURE_2D, texV_);
    if (uvW != vWidth_ || uvH != vHeight_) {
        gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, uvW, uvH, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        vWidth_  = uvW;
        vHeight_ = uvH;
    }
    gl_->glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[2] > 0 ? frame->linesize[2] : uvW);
    gl_->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, uvW, uvH, GL_RED, GL_UNSIGNED_BYTE, frame->data[2]);
    gl_->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void GLYuvShader::render(int viewportW, int viewportH)
{
    if (!ready_ || !program_ || !vao_ || !gl_) return;
    if (viewportW <= 0 || viewportH <= 0) return;

    // // 宽高比自适应：居中 + letterbox/pillarbox
    // int texW = yWidth_;
    // int texH = yHeight_;
    // if (texW > 0 && texH > 0) {
    //     float texAspect  = (float)texW / (float)texH;
    //     float viewAspect = (float)viewportW / (float)viewportH;
    //     int drawW = viewportW;
    //     int drawH = viewportH;
    //     if (viewAspect > texAspect) {
    //         drawW = (int)(viewportH * texAspect);
    //     } else {
    //         drawH = (int)(viewportW / texAspect);
    //     }
    //     int offsetX = (viewportW - drawW) / 2;
    //     int offsetY = (viewportH - drawH) / 2;

    //     // 先清除全屏（黑边），再在居中区域渲染视频
    //     gl_->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    //     gl_->glClear(GL_COLOR_BUFFER_BIT);
    //     gl_->glViewport(offsetX, offsetY, drawW, drawH);
    // } else {
    //     gl_->glViewport(0, 0, viewportW, viewportH);
    // }

    // 铺满整个窗口（不保持宽高比）
    gl_->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gl_->glClear(GL_COLOR_BUFFER_BIT);
    gl_->glViewport(0, 0, viewportW, viewportH);

    gl_->glUseProgram(program_);

    // 绑定 Y 纹理 (unit 0)
    gl_->glActiveTexture(GL_TEXTURE0);
    gl_->glBindTexture(GL_TEXTURE_2D, texY_);
    GLint locY = gl_->glGetUniformLocation(program_, "uTexY");
    if (locY >= 0) gl_->glUniform1i(locY, 0);

    // 绑定 U 纹理 (unit 1)
    gl_->glActiveTexture(GL_TEXTURE1);
    gl_->glBindTexture(GL_TEXTURE_2D, texU_);
    GLint locU = gl_->glGetUniformLocation(program_, "uTexU");
    if (locU >= 0) gl_->glUniform1i(locU, 1);

    // 绑定 V 纹理 (unit 2)
    gl_->glActiveTexture(GL_TEXTURE2);
    gl_->glBindTexture(GL_TEXTURE_2D, texV_);
    GLint locV = gl_->glGetUniformLocation(program_, "uTexV");
    if (locV >= 0) gl_->glUniform1i(locV, 2);

    gl_->glBindVertexArray(vao_);
    gl_->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl_->glBindVertexArray(0);
}

} // namespace muxplay
