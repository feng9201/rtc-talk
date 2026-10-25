import type { types as ms } from 'mediasoup';

/**
 * 音频 Opus + 单层 H.264（Constrained Baseline 42e01f）。
 * H.265 只走 P2P：mediasoup 3.16.6 已移除 HEVC，Router 注册会报 media codec not supported。
 * Opus channels=2 以匹配浏览器/libwebrtc 的 opus/48000/2。
 */
export const AUDIO_MEDIA_CODECS: ms.RouterRtpCodecCapability[] = [
  {
    kind: 'audio',
    mimeType: 'audio/opus',
    clockRate: 48000,
    channels: 2,
    parameters: { useinbandfec: 1, usedtx: 1 },
    rtcpFeedback: [{ type: 'transport-cc' }],
  },
];

/** 单层 H.264，不做 Simulcast。 */
export const VIDEO_MEDIA_CODECS: ms.RouterRtpCodecCapability[] = [
  {
    kind: 'video',
    mimeType: 'video/H264',
    clockRate: 90000,
    parameters: {
      'packetization-mode': 1,
      'profile-level-id': '42e01f',
      'level-asymmetry-allowed': 1,
    },
    rtcpFeedback: [
      { type: 'nack' },
      { type: 'nack', parameter: 'pli' },
      { type: 'ccm', parameter: 'fir' },
      { type: 'goog-remb' },
      { type: 'transport-cc' },
    ],
  },
];

export const MEDIA_CODECS: ms.RouterRtpCodecCapability[] = [...AUDIO_MEDIA_CODECS, ...VIDEO_MEDIA_CODECS];

/** SFU 可转发的视频编码，随 room.join 应答下发；P2P 不受此限制 */
export const SFU_VIDEO_CODECS = ['h264'] as const;

export const AUDIO_EXTENSIONS = new Set<string>([
  'urn:ietf:params:rtp-hdrext:sdes:mid',
  'urn:ietf:params:rtp-hdrext:ssrc-audio-level',
  'http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time',
  'http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01',
]);

export const VIDEO_EXTENSIONS = new Set<string>([
  'urn:ietf:params:rtp-hdrext:sdes:mid',
  'urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id',
  'urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id',
  'http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time',
  'http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01',
  'urn:3gpp:video-orientation',
  // 不列 toffset：mediasoup 3.13+ 的 RtpHeaderExtensionUri 枚举已移除该 URI，
  // 传给它会让 transport.produce() 抛 `invalid RtpHeaderExtensionUri`，
  // 整个 sfu.publish 失败（音视频全断）。libwebrtc 会主动带 toffset，
  // 由这里白名单过滤掉即可。
]);
