import type { types as ms } from 'mediasoup';

/**
 * 音频 Opus + 单层 H.264（Constrained Baseline 42e01f）。
 * H.265 按协议留到下一版，Router 里先不注册。
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
  'urn:ietf:params:rtp-hdrext:toffset',
]);
