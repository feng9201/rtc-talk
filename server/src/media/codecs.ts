import type { types as ms } from 'mediasoup';

/** 一期只开 Opus。参数与协议 9.1 对齐，channels=2 以匹配浏览器/libwebrtc 的 opus/48000/2。 */
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

export const AUDIO_EXTENSIONS = new Set<string>([
  'urn:ietf:params:rtp-hdrext:sdes:mid',
  'urn:ietf:params:rtp-hdrext:ssrc-audio-level',
  'http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time',
  'http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01',
]);
