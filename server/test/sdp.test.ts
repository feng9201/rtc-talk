import { describe, expect, it } from 'vitest';
import type { types as ms } from 'mediasoup';
import { TalkError } from '../src/gateway/protocol.js';
import { buildSdp, parseSdp } from '../src/media/sdp.js';

const FP = `${'A1:'.repeat(31)}A1`;

function offer(videoRtp: string, videoFmtp: string, videoExt: string[] = []): string {
  return [
    'v=0',
    'o=- 1 1 IN IP4 127.0.0.1',
    's=-',
    't=0 0',
    'a=group:BUNDLE 0 1',
    `a=fingerprint:sha-256 ${FP}`,
    'a=setup:actpass',
    'm=audio 9 UDP/TLS/RTP/SAVPF 111',
    'c=IN IP4 0.0.0.0',
    'a=mid:0',
    'a=sendonly',
    'a=rtpmap:111 opus/48000/2',
    'a=fmtp:111 useinbandfec=1;usedtx=1',
    'a=ssrc:1111 cname:audio',
    'm=video 9 UDP/TLS/RTP/SAVPF 102',
    'c=IN IP4 0.0.0.0',
    'a=mid:1',
    'a=sendonly',
    `a=rtpmap:102 ${videoRtp}`,
    `a=fmtp:102 ${videoFmtp}`,
    'a=rtcp-fb:102 nack',
    'a=rtcp-fb:102 nack pli',
    'a=ssrc:2222 cname:video',
    ...videoExt,
    '',
  ].join('\r\n');
}

const transport = {
  iceParameters: { usernameFragment: 'ufrag', password: 'pwd', iceLite: true },
  iceCandidates: [
    {
      foundation: '1',
      priority: 1,
      ip: '127.0.0.1',
      address: '127.0.0.1',
      protocol: 'udp' as const,
      port: 40000,
      type: 'host' as const,
    },
  ],
  dtlsParameters: {
    fingerprints: [{ algorithm: 'sha-256' as ms.FingerprintAlgorithm, value: FP }],
    role: 'server' as const,
  },
};

describe('video sdp', () => {
  it('parses single-layer h264 and answers with H264', () => {
    const parsed = parseSdp(
      offer('H264/90000', 'level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f'),
    );
    expect(parsed.tracks.map((t) => t.kind)).toEqual(['audio', 'video']);
    expect(parsed.tracks[1]?.rtpParameters?.codecs[0]).toMatchObject({
      mimeType: 'video/H264',
      clockRate: 90000,
      parameters: { 'packetization-mode': 1, 'profile-level-id': '42e01f', 'level-asymmetry-allowed': 1 },
    });
    const answer = buildSdp({
      transport,
      sections: parsed.tracks.map((t) => ({
        mid: t.mid,
        kind: t.kind,
        closed: t.closed,
        direction: 'recvonly' as const,
        rtpParameters: t.rtpParameters,
      })),
      setup: 'passive',
      offering: false,
      sessionId: '1',
      sessionVersion: 1,
    });
    expect(answer).toMatch(/m=video /);
    expect(answer).toMatch(/H264\/90000/);
    expect(answer).toMatch(/profile-level-id=42e01f/);
    expect(answer).toMatch(/a=recvonly/);
    expect(answer).toMatch(/a=setup:passive/);
  });

  it('rejects h265 until it is implemented', () => {
    expect(() => parseSdp(offer('H265/90000', 'profile-id=1'))).toThrow(TalkError);
    try {
      parseSdp(offer('H265/90000', 'profile-id=1'));
    } catch (err) {
      expect(err).toMatchObject({ code: 501, message: expect.stringContaining('h265') });
    }
  });

  it('rejects non-baseline h264', () => {
    expect(() => parseSdp(offer('H264/90000', 'packetization-mode=0;profile-level-id=42e01f'))).toThrow(
      expect.objectContaining({ code: 400 }),
    );
    expect(() => parseSdp(offer('H264/90000', 'packetization-mode=1;profile-level-id=64001f'))).toThrow(
      expect.objectContaining({ code: 400 }),
    );
  });

  it('filters header extensions mediasoup does not support (toffset)', () => {
    // libwebrtc 的视频 offer 会主动带 toffset；mediasoup 3.13+ 已移除该 URI，
    // 放行会导致 transport.produce() 抛 invalid RtpHeaderExtensionUri、整个 sfu.publish 失败
    const parsed = parseSdp(
      offer('H264/90000', 'level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f', [
        'a=extmap:2 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01',
        'a=extmap:3 urn:ietf:params:rtp-hdrext:toffset',
      ]),
    );
    const exts = parsed.tracks[1]?.rtpParameters?.headerExtensions ?? [];
    expect(exts.map((e) => e.uri)).toEqual([
      'http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01',
    ]);
  });
});
