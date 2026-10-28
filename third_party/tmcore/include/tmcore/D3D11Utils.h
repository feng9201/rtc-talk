#pragma once
#include "Export.h"

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace tmcore
{

/**
 * @brief 用指定显卡创建 D3D11 设备
 *
 * 设备标志参考 FFmpeg hwcontext_d3d11va.c：
 *   默认请求 VIDEO_SUPPORT | BGRA_SUPPORT（硬件解码 + WGL 共享纹理），
 *   失败时依次降级为仅 VIDEO、仅 BGRA、无标志。
 *
 * @param gpu_index   GpuInfo::index（DXGI 适配器索引）；传 -1 使用系统默认适配器
 * @param out_device  输出设备，调用方负责 Release
 * @param out_context 输出上下文，调用方负责 Release
 * @return true=创建成功
 */
TMCORE_API bool CreateD3D11Device(int gpu_index,
                                  ID3D11Device** out_device,
                                  ID3D11DeviceContext** out_context);

} // namespace tmcore
