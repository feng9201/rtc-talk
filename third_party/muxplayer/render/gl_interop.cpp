#include "gl_interop.h"
#include <d3d11.h>
#include <cstdio>
#include <cstring>

// 辅助：获取 D3D11 设备的 DXGI 适配器名称
static const char* getAdapterName(ID3D11Device* device) {
    if (!device) return "null";
    IDXGIDevice* dxgiDevice = nullptr;
    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice)))
        return "?(QI)";
    IDXGIAdapter* adapter = nullptr;
    dxgiDevice->GetAdapter(&adapter);
    dxgiDevice->Release();
    if (!adapter) return "?(adapter)";
    DXGI_ADAPTER_DESC desc = {};
    adapter->GetDesc(&desc);
    adapter->Release();
    static thread_local char buf[256];
    snprintf(buf, sizeof(buf), "%ls [VEN_%04X]", desc.Description, desc.VendorId);
    return buf;
}

namespace muxplay {

GLInterop::GLInterop() = default;

GLInterop::~GLInterop() {
    shutdown();
}

bool GLInterop::init() {
    // ── 检查 WGL_NV_DX_interop 扩展 ──
    const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
    if (!extensions || !strstr(extensions, "WGL_NV_DX_interop")) {
        typedef const char* (WINAPI* PFNWGLGETEXTENSIONSSTRINGEXTPROC)(HDC);
        auto wglGetExtensionsStringEXT = (PFNWGLGETEXTENSIONSSTRINGEXTPROC)
            wglGetProcAddress("wglGetExtensionsStringEXT");
        if (wglGetExtensionsStringEXT) {
            extensions = wglGetExtensionsStringEXT(wglGetCurrentDC());
        }
        if (!extensions || !strstr(extensions, "WGL_NV_DX_interop")) {
            return false;
        }
    }

    wglDXOpenDeviceNV_     = (PFNWGLDXOPENDEVICENVPROC)    wglGetProcAddress("wglDXOpenDeviceNV");
    wglDXCloseDeviceNV_    = (PFNWGLDXCLOSEDEVICENVPROC)   wglGetProcAddress("wglDXCloseDeviceNV");
    wglDXRegisterObjectNV_ = (PFNWGLDXREGISTEROBJECTNVPROC)wglGetProcAddress("wglDXRegisterObjectNV");
    wglDXUnregisterObjectNV_=(PFNWGLDXUNREGISTEROBJECTNVPROC)wglGetProcAddress("wglDXUnregisterObjectNV");
    wglDXLockObjectsNV_    = (PFNWGLDXLOCKOBJECTSNVPROC)   wglGetProcAddress("wglDXLockObjectsNV");
    wglDXUnlockObjectsNV_  = (PFNWGLDXUNLOCKOBJECTSNVPROC) wglGetProcAddress("wglDXUnlockObjectsNV");

    if (!wglDXOpenDeviceNV_ || !wglDXRegisterObjectNV_ ||
        !wglDXLockObjectsNV_ || !wglDXUnlockObjectsNV_ || !wglDXCloseDeviceNV_) {
        return false;
    }

    deviceReady_ = true;
    // 打印当前 GL 渲染器，便于跨 GPU 诊断
    const char* glRenderer = (const char*)glGetString(GL_RENDERER);
    const char* glVendor  = (const char*)glGetString(GL_VENDOR);
    printf("[GLInterop] GL renderer: %s (%s)\n", glRenderer ? glRenderer : "?", glVendor ? glVendor : "?");
    return true;
}

bool GLInterop::registerTexture(ID3D11Texture2D* d3dTexture, GLuint glTexName) {
    if (!deviceReady_ || !d3dTexture || !glTexName) return false;

    // 从纹理反查 D3D11 设备
    ID3D11Device* d3dDevice = nullptr;
    d3dTexture->GetDevice(&d3dDevice);
    if (!d3dDevice) return false;

    // ── 纹理变更检测 ──
    // 当 registeredTex_ 变化时（如 player close→open 重建 D3D 设备），
    // 旧 interopDevice_ 关联的是已销毁的 D3D 设备，必须关闭并重新打开。
    // wglDXOpenDeviceNV 内部对 D3D 设备 AddRef，close 时 Release，因此
    // 即使上层已释放设备，wgl 层持有引用仍可安全调用 CloseDevice。
    if (interopDevice_ && registeredTex_ != d3dTexture) {
        printf("[GLInterop] texture changed — closing old interop device\n");
        if (interopObject_ && wglDXUnregisterObjectNV_) {
            wglDXUnregisterObjectNV_(interopDevice_, interopObject_);
            interopObject_ = nullptr;
        }
        if (wglDXCloseDeviceNV_) {
            wglDXCloseDeviceNV_(interopDevice_);
            interopDevice_ = nullptr;
        }
        registeredTex_ = nullptr;
    }

    // 清理旧的 interop 对象（同设备内纹理替换路径，interopDevice_ 不变）
    if (interopObject_ && wglDXUnregisterObjectNV_) {
        wglDXUnregisterObjectNV_(interopDevice_, interopObject_);
        interopObject_ = nullptr;
    }

    // 打开 interop 设备（首次或设备变更后）
    if (!interopDevice_) {
        printf("[GLInterop] wglDXOpenDeviceNV(D3D11 on %s) ...\n", getAdapterName(d3dDevice));
        interopDevice_ = wglDXOpenDeviceNV_(d3dDevice);
        if (!interopDevice_) {
            printf("[GLInterop] wglDXOpenDeviceNV FAILED — D3D11 & GL 可能跨 GPU\n");
            d3dDevice->Release();
            return false;
        }
        printf("[GLInterop] wglDXOpenDeviceNV OK\n");
    }
    d3dDevice->Release();  // GetDevice 添加的引用

    // 注册 D3D 纹理为 GL 纹理（只读访问）
    interopObject_ = wglDXRegisterObjectNV_(
        interopDevice_,
        d3dTexture,
        glTexName,
        GL_TEXTURE_2D,
        WGL_ACCESS_READ_ONLY_NV);

    if (!interopObject_) {
        D3D11_TEXTURE2D_DESC texDesc = {};
        d3dTexture->GetDesc(&texDesc);
        printf("[GLInterop] wglDXRegisterObjectNV FAILED (fmt=%d, %ux%u)\n",
               (int)texDesc.Format, texDesc.Width, texDesc.Height);
        return false;
    }

    registeredTex_ = d3dTexture;
    return true;
}

void GLInterop::unregisterTexture() {
    if (interopObject_ && wglDXUnregisterObjectNV_) {
        wglDXUnregisterObjectNV_(interopDevice_, interopObject_);
        interopObject_ = nullptr;
    }
    registeredTex_ = nullptr;
}

bool GLInterop::lock() {
    if (!interopObject_ || !wglDXLockObjectsNV_) return false;
    return wglDXLockObjectsNV_(interopDevice_, 1, &interopObject_) != FALSE;
}

bool GLInterop::unlock() {
    if (!interopObject_ || !wglDXUnlockObjectsNV_) return false;
    return wglDXUnlockObjectsNV_(interopDevice_, 1, &interopObject_) != FALSE;
}

void GLInterop::shutdown() {
    unregisterTexture();
    if (interopDevice_ && wglDXCloseDeviceNV_) {
        wglDXCloseDeviceNV_(interopDevice_);
        interopDevice_ = nullptr;
    }
    deviceReady_ = false;
}

} // namespace muxplay
