#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <chrono>

#include "../multiplex_player_private.h"


namespace muxplay
{
	class DxRender
	{
	public:
		DxRender(HWND hwnd);
		~DxRender();
		bool InitRender(size_t width, size_t height, ID3D11Device* sharedDevice, ID3D11DeviceContext* sharedContext);
		bool renderVideo(AVFrame* frame);
		void present();
		void destroy();

		// ── 防闪烁：由 D3DRenderWidget 在 WM_SIZE 时主动调用，即时 ResizeBuffers ──
		void ResizeToRect(uint32_t width, uint32_t height);

		// ── D3D 上下文访问（供 OverlayManager 使用）──
		ID3D11DeviceContext* context() const { return d3dImmediateContext_.Get(); }
		ID3D11Device*       device()  const { return d3dDevice_.Get(); }
		uint32_t renderWidth()  const { return renderWidth_; }
		uint32_t renderHeight() const { return renderHeight_; }

		// ── GL 互通接口 ──
		ID3D11Texture2D* getSharedTexture() const { return d3dSharedRGBA_.Get(); }
		void setGLInteropMode(bool enabled) { useGLInterop_ = enabled; }
		bool getGLInteropMode() const { return useGLInterop_; }

		// ── 画中画（PIP）接口 ──
		// 启用/禁用 PIP 小窗口，PIP 使用同一帧 NV12 纹理在指定位置二次绘制
		void enablePip(bool enabled) { pipEnabled_ = enabled; }
		bool isPipEnabled() const { return pipEnabled_; }
		// 设置 PIP 矩形区域（逻辑像素坐标，内部按 dpiScale 转物理像素）
		void setPipRect(const RECT& rect) { pipRect_ = rect; }
		const RECT& getPipRect() const { return pipRect_; }
		// 设置 DPI 缩放比（与 OverlayManager 一致，逻辑→物理像素）
		void setDpiScale(float scale) { dpiScale_ = scale; }

		void setUUid(const std::string& uuid) { uuid_ = uuid; }
	private:
		bool HandleDeviceLost();  // P1: Device Lost 恢复

		HWND hwnd_ = nullptr;

		// ── P1: ComPtr 智能指针，自动管理引用计数，消除手动 AddRef/Release ──
		Microsoft::WRL::ComPtr<ID3D11Device>           d3dDevice_;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext>    d3dImmediateContext_;
		Microsoft::WRL::ComPtr<IDXGIDevice>            d3dgiDevice_;
		Microsoft::WRL::ComPtr<IDXGIAdapter>           d3dgiAdapter_;
		Microsoft::WRL::ComPtr<IDXGIFactory>           d3dgiFactory_;
		Microsoft::WRL::ComPtr<IDXGISwapChain>         d3dSwapChain_;
		Microsoft::WRL::ComPtr<ID3D11VertexShader>     d3dVertexShader_;
		Microsoft::WRL::ComPtr<ID3DBlob>               d3dBlob_;
		Microsoft::WRL::ComPtr<ID3D11PixelShader>      d3dPixelShader_;
		Microsoft::WRL::ComPtr<ID3D11PixelShader>      d3dPixelShaderBT709_;
		Microsoft::WRL::ComPtr<ID3D11PixelShader>      d3dPixelShaderBT2020_;
		Microsoft::WRL::ComPtr<ID3D11Texture2D>        d3dTextureNV12_;
		Microsoft::WRL::ComPtr<ID3D11Texture2D>        d3dSharedRGBA_;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> d3dShaderResourceViewY_;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> d3dShaderResourceViewUV_;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> d3dRenderTargetView_;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> d3dSharedRTV_;
		Microsoft::WRL::ComPtr<ID3D11Buffer>           vertexBuffer_;
		Microsoft::WRL::ComPtr<ID3D11InputLayout>      inputLayout_;
		Microsoft::WRL::ComPtr<ID3D11SamplerState>     samplerState_;
		Microsoft::WRL::ComPtr<ID3D11BlendState>       blendState_;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState>  rasterizerState_;

		// ── PIP 画中画资源 ──
		Microsoft::WRL::ComPtr<ID3D11Buffer> pipVB_;
		bool     pipEnabled_ = false;
		RECT     pipRect_ = {0, 0, 0, 0};     // 逻辑像素坐标
		float    dpiScale_ = 1.0f;            // 高 DPI 缩放比

		uint32_t renderWidth_ = 0;
		uint32_t renderHeight_ = 0;
		bool     shadersReady_ = false;
		bool     useGLInterop_ = false;
		bool     deviceLost_ = false;  // P1: Device Lost 标志
		std::string uuid_;//播放器标志
	};
}


