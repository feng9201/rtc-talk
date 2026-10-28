#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <GL/gl.h>

namespace muxplay {

// ── WGL_NV_DX_interop 最小封装 ──
// 只负责：打开D3D设备、注册/注销纹理、锁定/解锁
// 不包含任何 OpenGL 渲染代码（shader/VAO/Draw 由调用方负责）
class GLInterop {
public:
    GLInterop();
    ~GLInterop();

    // 加载 WGL 扩展（调用前提：当前线程有活跃 GL 上下文）
    bool init();

    // 注册 D3D11 共享纹理 → 绑定到 GL 纹理名
    // 返回是否成功。成功后将 glTexName 与 d3dTexture 关联
    bool registerTexture(ID3D11Texture2D* d3dTexture, GLuint glTexName);

    // 注销当前纹理
    void unregisterTexture();

    // 锁定 interop 对象（D3D→GL 同步，GL 可安全读取）
    bool lock();

    // 解锁 interop 对象（释放给 D3D 写入）
    bool unlock();

    // 释放 interop device
    void shutdown();

    bool isReady() const { return deviceReady_; }

    // 获取当前关联的 D3D11 纹理（用于变更检测）
    ID3D11Texture2D* registeredTex() const { return registeredTex_; }

private:
    // ── WGL 函数指针 ──
    typedef HANDLE (WINAPI* PFNWGLDXOPENDEVICENVPROC)(void* dxDevice);
    typedef BOOL   (WINAPI* PFNWGLDXCLOSEDEVICENVPROC)(HANDLE hDevice);
    typedef HANDLE (WINAPI* PFNWGLDXREGISTEROBJECTNVPROC)(HANDLE hDevice, void* dxObject, GLuint name, GLenum type, GLenum access);
    typedef BOOL   (WINAPI* PFNWGLDXUNREGISTEROBJECTNVPROC)(HANDLE hDevice, HANDLE hObject);
    typedef BOOL   (WINAPI* PFNWGLDXLOCKOBJECTSNVPROC)(HANDLE hDevice, GLsizei count, HANDLE* hObjects);
    typedef BOOL   (WINAPI* PFNWGLDXUNLOCKOBJECTSNVPROC)(HANDLE hDevice, GLsizei count, HANDLE* hObjects);

    PFNWGLDXOPENDEVICENVPROC         wglDXOpenDeviceNV_ = nullptr;
    PFNWGLDXCLOSEDEVICENVPROC        wglDXCloseDeviceNV_ = nullptr;
    PFNWGLDXREGISTEROBJECTNVPROC     wglDXRegisterObjectNV_ = nullptr;
    PFNWGLDXUNREGISTEROBJECTNVPROC   wglDXUnregisterObjectNV_ = nullptr;
    PFNWGLDXLOCKOBJECTSNVPROC        wglDXLockObjectsNV_ = nullptr;
    PFNWGLDXUNLOCKOBJECTSNVPROC      wglDXUnlockObjectsNV_ = nullptr;

    HANDLE  interopDevice_ = nullptr;
    HANDLE  interopObject_ = nullptr;
    bool    deviceReady_ = false;
    ID3D11Texture2D* registeredTex_ = nullptr;
};

// WGL_NV_DX_interop 常量
#ifndef WGL_NV_DX_interop
#define WGL_ACCESS_READ_ONLY_NV     0x0000
#define WGL_ACCESS_READ_WRITE_NV    0x0001
#define WGL_ACCESS_WRITE_DISCARD_NV 0x0002
#endif

} // namespace muxplay
