#include "hw_decoder.h"
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/hwcontext.h>
#include <w_log_cpp.h>

extern "C" {
#include <libavcodec/avcodec.h>
}

// ── SPS 解析函数 ──
// h264_decode_sps 实现在 h264_spsDecoder.h（通过 h264_sps_decoder_wrapper.cpp 编译）
// ParseSequenceParameterSet + vc_params_t 实现在 hevc_spsDecoder.h（pragma once，直接包含）
#include "../demuxing/hevc_spsDecoder.h"

extern bool h264_decode_sps(unsigned char* src_buf, unsigned int nLen,
                            int& width, int& height, int& fps, int& max_ref_frames);


namespace muxplay {

HardwareDecoder::HardwareDecoder(MultiplexPlayerPrivate* priv)
    : priv_(priv)
{
}

HardwareDecoder::~HardwareDecoder()
{
    close();
}

// ── D3D11VA HW pixel format 回调 ──
AVPixelFormat HardwareDecoder::getHWFormat(AVCodecContext* s, const enum AVPixelFormat* fmt)
{
    auto self = static_cast<HardwareDecoder*>(s->opaque);
    const enum AVPixelFormat* p;
    for (p = fmt; *p != -1; p++) {
        if (*p == self->pix_format_) {
            return *p;
        }
    }
    // 硬件格式不可用，FFmpeg 会自动选择 choices[0] 作为 fallback
    LOG_WARN("[hw_decoder] [uuid={}] D3D11VA 格式不可用，FFmpeg 将自动降级到软件格式={}",
             self->priv_->uuid_, (int)fmt[0]);
    return fmt[0];
}

// ── D3D11VA 设备初始化（从 FFmpegDemuxing 迁移）──
bool HardwareDecoder::initHWDevice(const AVCodec* codec, ID3D11Device* sharedDevice)
{
    if (!codec) return false;

    for (int i = 0; ; i++) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
        if (!config) return false;

        // 只使用 D3D11VA，跳过 DXVA2（防止 hwctx 类型不匹配导致访问冲突）
        if (config->device_type != AV_HWDEVICE_TYPE_D3D11VA)
            continue;

        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) {
            pix_format_ = config->pix_fmt;
            LOG_INFO("[hw_decoder] [uuid={}] hw config[{}]: device_type={} pix_fmt={} methods=0x{:x}",
                     priv_->uuid_, i, (int)config->device_type, (int)config->pix_fmt, config->methods);

            if (sharedDevice) {
                // 使用外部传入的 D3D11 设备
                hw_device_ctx_ = av_hwdevice_ctx_alloc(config->device_type);
                if (!hw_device_ctx_) {
                    LOG_ERR("[hw_decoder] [uuid={}] 分配 D3D11VA 设备上下文失败", priv_->uuid_);
                    return false;
                }

                AVHWDeviceContext* hw_ctx = (AVHWDeviceContext*)hw_device_ctx_->data;
                AVD3D11VADeviceContext* d3d11_ctx = (AVD3D11VADeviceContext*)hw_ctx->hwctx;
                d3d11_ctx->device = sharedDevice;
                sharedDevice->AddRef();
                d3d11_ctx->device_context = priv_->d3dContext_;
                if (d3d11_ctx->device_context)
                    d3d11_ctx->device_context->AddRef();

                int ret = av_hwdevice_ctx_init(hw_device_ctx_);
                if (ret < 0) {
                    av_buffer_unref(&hw_device_ctx_);
                    hw_device_ctx_ = nullptr;
                    LOG_ERR("[hw_decoder] [uuid={}] 初始化 D3D11VA 设备失败", priv_->uuid_);
                    return false;
                }
            } else {
                // 让 FFmpeg 自行创建 D3D11 设备（内部处理兼容性标志）
                int ret = av_hwdevice_ctx_create(&hw_device_ctx_, config->device_type,
                                                  nullptr, nullptr, 0);
                if (ret < 0) {
                    LOG_ERR("[hw_decoder] [uuid={}] 创建 D3D11VA 设备失败", priv_->uuid_);
                    return false;
                }
            }

            // 从硬件设备上下文提取 D3D11 设备，供渲染器零拷贝复用
            if (!priv_->d3dDevice_) {
                AVHWDeviceContext* hw_ctx = (AVHWDeviceContext*)hw_device_ctx_->data;
                AVD3D11VADeviceContext* d3d11_ctx = (AVD3D11VADeviceContext*)hw_ctx->hwctx;
                if (d3d11_ctx->device) {
                    priv_->d3dDevice_ = d3d11_ctx->device;
                    priv_->d3dDevice_->AddRef();
                }
                if (d3d11_ctx->device_context) {
                    priv_->d3dContext_ = d3d11_ctx->device_context;
                    priv_->d3dContext_->AddRef();
                }
            }
            return true;
        }
    }
}

// ── 初始化：创建 codecContext + D3D11VA 设备 + avcodec_open2 ──
bool HardwareDecoder::init()
{
    if (!priv_) return false;

    // ── 无 AVFormatContext 模式（FLV 拉流 / 外部流注入）──
    // codecContext_ 已由外部（openFlv / injectExternalStream）创建并配置好 extradata
    if (priv_->isInjectStream_.load(std::memory_order_acquire)
#ifdef MUXPLAY_ENABLE_FLV
        || priv_->isFlvStream_
#endif
    ) {
        if (!priv_->codecContext_) return false;

        // ── 从 extradata 预解析 SPS 获取分辨率、profile、level ──
        // FFmpeg 的 h264_decode_init 解析 extradata 时存储 SPS 到内部结构，
        // 但 avctx->profile/level 在某些版本中不在 open2 时设置，而是延迟到首次解码帧时。
        // D3D11VA 初始化需要 profile 来查找 codec GUID，如果 profile=-99 会导致初始化失败。
        // 解决方案：手动解析 SPS，预设 width/height/profile/level/sw_pix_fmt 到 codecContext_。
        AVCodecContext* ctx = priv_->codecContext_;
        if (ctx->width <= 0 || ctx->height <= 0) {
            const uint8_t* ed = ctx->extradata;
            int edSize = ctx->extradata_size;
            if (ed && edSize >= 7 && ed[0] == 0x01) {
                // H.264 AVCDecoderConfigurationRecord
                if (ctx->codec_id == AV_CODEC_ID_H264) {
                    // 偏移 5: numOfSPS, 偏移 6-7: SPS 长度, 偏移 8+: SPS NALU
                    int numSPS = ed[5] & 0x1f;
                    if (numSPS >= 1) {
                        int spsLen = (ed[6] << 8) | ed[7];
                        if (spsLen > 0 && 8 + spsLen <= edSize) {
                            const uint8_t* spsNalu = ed + 8;
                            int w = 0, h = 0, fps = 0, maxRef = 0;
                            if (h264_decode_sps(const_cast<unsigned char*>(spsNalu), spsLen, w, h, fps, maxRef)) {
                                ctx->width = w;
                                ctx->height = h;
                                // 从 SPS NALU 提取 profile_idc 和 level_idc
                                // SPS NALU 格式: [nal_header(1)] [profile_idc(1)] [constraint(1)] [level_idc(1)] ...
                                if (spsLen >= 4) {
                                    int profile_idc = spsNalu[1];  // 0x42 = 66 = Baseline
                                    int level_idc = spsNalu[3];    // 0x29 = 41 = 4.1
                                    ctx->profile = profile_idc;
                                    ctx->level = level_idc;
                                    // D3D11VA 解码输出 NV12 格式纹理（不是 YUV420P 平面格式）
                                    // sw_pix_fmt 用于让 FFmpeg 知道软解时的输出格式，
                                    // 但 hw_frames_ctx 的 sw_format 必须是 NV12
                                    ctx->sw_pix_fmt = AV_PIX_FMT_NV12;
                                }
                                LOG_INFO("[hw_decoder] [uuid={}] SPS 预解析: w={} h={} profile={} level={} maxRef={} (H.264)",
                                         priv_->uuid_, w, h, ctx->profile, ctx->level, maxRef);
                            } else {
                                LOG_WARN("[hw_decoder] [uuid={}] H.264 SPS 预解析失败", priv_->uuid_);
                            }
                        }
                    }
                }
                // HEVC HEVCDecoderConfigurationRecord
                else if (ctx->codec_id == AV_CODEC_ID_HEVC) {
                    // HEVC HVCC 格式：偏移 22: numOfArrays, 偏移 23+: array entries
                    // 每个 array: 1 byte type, 2 bytes numNalus, 然后每个 NALU: 2 bytes length + data
                    // SPS NAL type = 33 (HEVC_NAL_SPS)
                    if (edSize >= 23) {
                        int numArrays = ed[22];
                        int pos = 23;
                        for (int i = 0; i < numArrays && pos + 3 <= edSize; i++) {
                            uint8_t arrayType = ed[pos] & 0x3f;
                            int numNalus = (ed[pos + 1] << 8) | ed[pos + 2];
                            pos += 3;
                            for (int j = 0; j < numNalus && pos + 2 <= edSize; j++) {
                                int naluLen = (ed[pos] << 8) | ed[pos + 1];
                                pos += 2;
                                if (arrayType == 33 && naluLen > 0 && pos + naluLen <= edSize) {
                                    // SPS NALU found
                                    vc_params_t params;
                                    if (ParseSequenceParameterSet(ed + pos, naluLen, params)) {
                                        ctx->width = params.width;
                                        ctx->height = params.height;
                                        ctx->profile = params.profile;
                                        ctx->level = params.level;
                                        ctx->sw_pix_fmt = AV_PIX_FMT_NV12;
                                        LOG_INFO("[hw_decoder] [uuid={}] SPS 预解析: w={} h={} profile={} level={} (HEVC)",
                                                 priv_->uuid_, params.width, params.height, params.profile, params.level);
                                    }
                                }
                                pos += naluLen;
                            }
                        }
                    }
                }
            }
        }

        priv_->video_width_  = ctx->width;
        priv_->video_height_ = ctx->height;

        LOG_INFO("[hw_decoder] [uuid={}] FLV init: w={} h={} extradata_size={} codec={}",
                 priv_->uuid_, ctx->width, ctx->height, ctx->extradata_size,
                 ctx->codec ? ctx->codec->name : "null");

        if (!initHWDevice(ctx->codec, priv_->d3dDevice_)){
            LOG_ERR("[hw_decoder] [uuid={}] 初始化 D3D11VA 设备失败", priv_->uuid_);
            return false;
        }

        ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx_);
        ctx->get_format = &HardwareDecoder::getHWFormat;
        ctx->opaque = this;
        ctx->thread_count = 1;
        ctx->thread_type = FF_THREAD_SLICE;
        ctx->err_recognition = 0;

        if (avcodec_open2(ctx, ctx->codec, nullptr) < 0){
            LOG_ERR("[hw_decoder] [uuid={}] 打开解码器失败", priv_->uuid_);
            return false;
        }

        LOG_INFO("[hw_decoder] [uuid={}] after open2: w={} h={} pix_fmt={} sw_pix_fmt={} profile={} level={}",
                 priv_->uuid_,
                 ctx->width, ctx->height, (int)ctx->pix_fmt, (int)ctx->sw_pix_fmt, ctx->profile, ctx->level);

        // ── 手动创建 hw_frames_ctx（D3D11VA 表面池）──
        // FFmpeg 5.x 的 D3D11VA hwaccel->init 需要 hw_frames_ctx（包含解码表面池）。
        if (!ctx->hw_frames_ctx && hw_device_ctx_) {
            LOG_INFO("[hw_decoder] [uuid={}] 创建 hw_frames_ctx: pix_format_={} w={} h={} d3dDevice={:p} d3dContext={:p}",
                     priv_->uuid_, (int)pix_format_, ctx->width, ctx->height,
                     (void*)priv_->d3dDevice_, (void*)priv_->d3dContext_);

            AVBufferRef* frames_ref = av_hwframe_ctx_alloc(hw_device_ctx_);
            if (frames_ref) {
                AVHWFramesContext* frames_ctx = (AVHWFramesContext*)frames_ref->data;
                frames_ctx->format = (AVPixelFormat)pix_format_;
                frames_ctx->sw_format = AV_PIX_FMT_NV12;
                frames_ctx->width = ctx->width;
                frames_ctx->height = ctx->height;
                frames_ctx->initial_pool_size = 20;

                // 打印 D3D11VAFramesContext 详情
                AVD3D11VAFramesContext* d3d11_frames = (AVD3D11VAFramesContext*)frames_ctx->hwctx;
                d3d11_frames->BindFlags = D3D11_BIND_DECODER;
                d3d11_frames->MiscFlags = 0;

                int ret = av_hwframe_ctx_init(frames_ref);
                if (ret >= 0) {
                    ctx->hw_frames_ctx = frames_ref;
                    LOG_INFO("[hw_decoder] [uuid={}] hw_frames_ctx 创建成功: w={} h={} format={} sw_format={}",
                             priv_->uuid_, ctx->width, ctx->height,
                             (int)pix_format_, (int)AV_PIX_FMT_NV12);
                } else {
                    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                    av_strerror(ret, errbuf, sizeof(errbuf));
                    av_buffer_unref(&frames_ref);
                    LOG_ERR("[hw_decoder] [uuid={}] hw_frames_ctx 创建失败: {} ({}) "
                             "format={} sw_format={} w={} h={}",
                             priv_->uuid_, ret, errbuf,
                             (int)pix_format_, (int)AV_PIX_FMT_NV12, ctx->width, ctx->height);
                }
            } else {
                LOG_ERR("[hw_decoder] [uuid={}] av_hwframe_ctx_alloc 失败", priv_->uuid_);
            }
        } else if (ctx->hw_frames_ctx) {
            LOG_INFO("[hw_decoder] [uuid={}] hw_frames_ctx 已由 open2 自动创建", priv_->uuid_);
        } else {
            LOG_ERR("[hw_decoder] [uuid={}] 无法创建 hw_frames_ctx: hw_device_ctx 为空", priv_->uuid_);
        }

        ffmpegDecoder_ = std::make_unique<FFmpegDecoder>(priv_);
        inited_ = true;
        return true;
    }

    if (!priv_->formatContext_) return false;
    if (priv_->videoStreamIndex_ < 0) return false;

    AVCodecParameters* codecpar =
        priv_->formatContext_->streams[priv_->videoStreamIndex_]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        LOG_ERR("[hw_decoder] [uuid={}] 无法找到解码器", priv_->uuid_);
        return false;
    }

    // 创建解码器上下文
    priv_->codecContext_ = avcodec_alloc_context3(codec);
    if (avcodec_parameters_to_context(priv_->codecContext_, codecpar) != 0) {
        LOG_ERR("[hw_decoder] [uuid={}] 无法将编解码器参数复制到解码器上下文", priv_->uuid_);
        return false;
    }

    // 读取分辨率
    int width  = priv_->codecContext_->width;
    int height = priv_->codecContext_->height;
    if (width <= 0 || height <= 0) {
        width  = codecpar->width;
        height = codecpar->height;
    }
    priv_->video_width_  = width;
    priv_->video_height_ = height;

    // 初始化 D3D11VA 硬件设备
    if (!initHWDevice(codec, priv_->d3dDevice_)) {
        LOG_ERR("[hw_decoder] [uuid={}] 初始化 D3D11VA 设备失败", priv_->uuid_);
        return false;
    }

    // 绑定 HW 上下文到解码器
    priv_->codecContext_->hw_device_ctx = av_buffer_ref(hw_device_ctx_);
    priv_->codecContext_->get_format = &HardwareDecoder::getHWFormat;
    priv_->codecContext_->opaque = this;

    // 打开解码器
    if (avcodec_open2(priv_->codecContext_, codec, nullptr) < 0) {
        LOG_ERR("[hw_decoder] [uuid={}] 打开解码器失败", priv_->uuid_);
        return false;
    }

    LOG_INFO("[hw_decoder] [uuid={}] FFmpeg path after open2: w={} h={} pix_fmt={} sw_pix_fmt={} profile={} level={} hw_frames_ctx={:p}",
             priv_->uuid_,
             priv_->codecContext_->width, priv_->codecContext_->height,
             (int)priv_->codecContext_->pix_fmt, (int)priv_->codecContext_->sw_pix_fmt,
             priv_->codecContext_->profile, priv_->codecContext_->level,
             (void*)priv_->codecContext_->hw_frames_ctx);

    // ── 手动创建 hw_frames_ctx（如果 open2 没有自动创建）──
    if (!priv_->codecContext_->hw_frames_ctx && hw_device_ctx_) {
        AVBufferRef* frames_ref = av_hwframe_ctx_alloc(hw_device_ctx_);
        if (frames_ref) {
            AVHWFramesContext* frames_ctx = (AVHWFramesContext*)frames_ref->data;
            frames_ctx->format = (AVPixelFormat)pix_format_;
            frames_ctx->sw_format = AV_PIX_FMT_NV12;
            frames_ctx->width = priv_->codecContext_->width;
            frames_ctx->height = priv_->codecContext_->height;
            frames_ctx->initial_pool_size = 20;
            AVD3D11VAFramesContext* d3d11_frames = (AVD3D11VAFramesContext*)frames_ctx->hwctx;
            d3d11_frames->BindFlags = D3D11_BIND_DECODER;
            d3d11_frames->MiscFlags = 0;

            int ret = av_hwframe_ctx_init(frames_ref);
            if (ret >= 0) {
                priv_->codecContext_->hw_frames_ctx = frames_ref;
                LOG_INFO("[hw_decoder] [uuid={}] FFmpeg path hw_frames_ctx 创建成功", priv_->uuid_);
            } else {
                char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(ret, errbuf, sizeof(errbuf));
                av_buffer_unref(&frames_ref);
                LOG_ERR("[hw_decoder] [uuid={}] FFmpeg path hw_frames_ctx 创建失败: {} ({})",
                         priv_->uuid_, ret, errbuf);
            }
        }
    }

    ffmpegDecoder_ = std::make_unique<FFmpegDecoder>(priv_);
    inited_ = true;
    return true;
}

// ── IDecoder 接口实现 ──
DecodeResult HardwareDecoder::decode(DecodedFrame& out)
{
    if (!inited_ || !ffmpegDecoder_) return DecodeResult::Error;

    out.avframe = av_frame_alloc();
    DecodeResult result = ffmpegDecoder_->VideoDecoder(out.avframe);

    if (result == DecodeResult::GotFrame) {
        // 根据实际帧格式判断内存类型（D3D11VA 可能降级为软件解码）
        out.memoryType = (out.avframe->format == AV_PIX_FMT_D3D11)
            ? FrameMemoryType::D3D11_Texture
            : FrameMemoryType::CPU_Buffer;
    } else {
        av_frame_free(&out.avframe);
        out.avframe = nullptr;
    }
    return result;
}

bool HardwareDecoder::hasPendingPacket() const
{
    return ffmpegDecoder_ ? ffmpegDecoder_->hasPendingPacket() : false;
}

void HardwareDecoder::close()
{
    LOG_INFO("[hw_decoder] [uuid={}] 关闭硬件解码器", priv_->uuid_);
    if (ffmpegDecoder_) {
        ffmpegDecoder_->close();
        ffmpegDecoder_.reset();
    }
    if (hw_device_ctx_) {
        av_buffer_unref(&hw_device_ctx_);
        hw_device_ctx_ = nullptr;
    }
    inited_ = false;
}

} // namespace muxplay
