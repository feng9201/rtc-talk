import * as sdpTransform from 'sdp-transform';
import type { types as ms } from 'mediasoup';
import { TalkError } from '../gateway/protocol.js';
import { AUDIO_EXTENSIONS, VIDEO_EXTENSIONS } from './codecs.js';

export interface ParsedTrack {
  mid: string;
  kind: 'audio' | 'video';
  direction: string;
  closed: boolean;
  rtpParameters?: ms.RtpParameters;
}

export interface ParsedSdp {
  dtls: ms.DtlsParameters;
  iceUfrag: string;
  tracks: ParsedTrack[];
}

function numParams(raw: Record<string, unknown>): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  for (const [k, v] of Object.entries(raw)) {
    if (typeof v === 'string' && v !== '' && !Number.isNaN(Number(v))) out[k] = Number(v);
    else out[k] = v;
  }
  return out;
}

function dtlsRole(setup: string | undefined): ms.DtlsRole {
  if (setup === 'active') return 'client';
  if (setup === 'passive') return 'server';
  return 'auto';
}

/** 客户端 offer/answer → DTLS 参数 + 每条 m-line 的 RTP 参数 */
export function parseSdp(sdp: string): ParsedSdp {
  let sdpObject: sdpTransform.SessionDescription;
  try {
    sdpObject = sdpTransform.parse(sdp);
  } catch {
    throw new TalkError(400, 'invalid sdp');
  }
  const media = (sdpObject.media ?? []).find((m) => m.port !== 0);
  const setup = sdpObject.setup ?? media?.setup;
  const fingerprint = sdpObject.fingerprint ?? media?.fingerprint;
  if (!setup || !fingerprint) throw new TalkError(400, 'sdp missing dtls setup/fingerprint');

  const iceUfrag = media?.iceUfrag ?? sdpObject.media?.[0]?.iceUfrag ?? '';
  const tracks: ParsedTrack[] = [];

  for (const m of sdpObject.media ?? []) {
    const kind = m.type === 'video' ? 'video' : m.type === 'audio' ? 'audio' : undefined;
    // sdp-transform 会把纯数字 mid 解析成 number；统一转 string，与客户端
    // tracks[].mid、协议中的 mid 一致，否则严格比较错配（两路视频被判成同 source）
    const mid = String(m.mid ?? tracks.length);
    const direction = m.direction ?? 'sendrecv';
    if (!kind) {
      tracks.push({ mid, kind: 'audio', direction: 'inactive', closed: true });
      continue;
    }
    const closed = m.port === 0 || direction === 'inactive';
    // 接收端 answer 是 recvonly，没有 ssrc；只在发送方向提取 RTP 参数
    if (closed || direction === 'recvonly') {
      tracks.push({ mid, kind, direction, closed: true });
      continue;
    }
    const wanted = kind === 'video' ? 'h264' : 'opus';
    const rtp = (m.rtp ?? []).find((r) => r.codec.toLowerCase() === wanted);
    if (!rtp) {
      const h265 = (m.rtp ?? []).some((r) => ['h265', 'hev1', 'hvc1'].includes(r.codec.toLowerCase()));
      if (kind === 'video' && h265) throw new TalkError(501, 'h265 is not supported yet');
      throw new TalkError(400, `m-line ${mid} has no ${wanted}`);
    }
    const fmtp = (m.fmtp ?? []).find((f) => f.payload === rtp.payload);
    const parameters = fmtp ? numParams(sdpTransform.parseParams(fmtp.config)) : {};
    if (kind === 'audio') {
      parameters.useinbandfec ??= 1;
      parameters.usedtx ??= 1;
    } else {
      parameters['packetization-mode'] ??= 1;
      parameters['level-asymmetry-allowed'] ??= 1;
      parameters['profile-level-id'] ??= '42e01f';
      if (Number(parameters['packetization-mode']) !== 1) {
        throw new TalkError(400, `m-line ${mid} h264 packetization-mode must be 1`);
      }
      if (String(parameters['profile-level-id']).toLowerCase() !== '42e01f') {
        throw new TalkError(400, `m-line ${mid} h264 profile-level-id must be 42e01f`);
      }
    }
    const feedback = (m.rtcpFb ?? [])
      .filter((fb) => fb.payload === rtp.payload || fb.payload === '*')
      .map((fb) => (fb.subtype ? { type: fb.type, parameter: fb.subtype } : { type: fb.type }));
    const allow = kind === 'video' ? VIDEO_EXTENSIONS : AUDIO_EXTENSIONS;
    const headerExtensions = (m.ext ?? [])
      .filter((e) => !e['encrypt-uri'] && allow.has(e.uri))
      .map((e) => ({ uri: e.uri as ms.RtpHeaderExtensionUri, id: e.value }));
    const ssrcLine = (m.ssrcs ?? []).find((s) => s.attribute === 'cname');
    const ssrc = ssrcLine?.id ?? m.ssrcs?.[0]?.id;
    if (!ssrc) throw new TalkError(400, `m-line ${mid} has no ssrc`);
    const cname = ssrcLine?.value || `talk-${ssrc}`;
    tracks.push({
      mid,
      kind,
      direction: m.direction ?? 'sendrecv',
      closed: false,
      rtpParameters: {
        codecs: [
          kind === 'video'
            ? {
                mimeType: 'video/H264',
                payloadType: rtp.payload,
                clockRate: rtp.rate ?? 90000,
                parameters,
                rtcpFeedback:
                  feedback.length > 0
                    ? feedback
                    : [{ type: 'nack' }, { type: 'nack', parameter: 'pli' }, { type: 'transport-cc' }],
              }
            : {
                mimeType: 'audio/opus',
                payloadType: rtp.payload,
                clockRate: rtp.rate ?? 48000,
                channels: rtp.encoding ?? 2,
                parameters,
                rtcpFeedback: feedback.length > 0 ? feedback : [{ type: 'transport-cc' }],
              },
        ],
        headerExtensions,
        encodings: [{ ssrc }],
        rtcp: { cname, reducedSize: true },
      },
    });
  }

  // offer 里 setup=actpass 时，让对端当 DTLS client，mediasoup 当 server，answer 用 passive
  const role = dtlsRole(setup);
  return {
    iceUfrag,
    dtls: {
      role: role === 'auto' ? 'client' : role,
      fingerprints: [{ algorithm: fingerprint.type as ms.FingerprintAlgorithm, value: fingerprint.hash }],
    },
    tracks,
  };
}

interface TransportView {
  iceParameters: ms.IceParameters;
  iceCandidates: ms.IceCandidate[];
  dtlsParameters: ms.DtlsParameters;
}

export interface SdpSection {
  mid: string;
  kind: 'audio' | 'video';
  closed: boolean;
  /** 本端方向：answer 对 sendonly offer 用 recvonly；recv offer 用 sendonly */
  direction: 'sendonly' | 'recvonly' | 'inactive';
  rtpParameters?: ms.RtpParameters;
  streamId?: string;
  trackId?: string;
}

function mediaFromSection(
  section: SdpSection,
  transport: TransportView,
  ip: string,
  port: number,
  setup: string,
  offering: boolean,
): sdpTransform.MediaDescription {
  const video = section.kind === 'video';
  const codec = section.rtpParameters?.codecs[0];
  const pt = codec?.payloadType ?? (video ? 102 : 111);
  const inactive = section.closed || section.direction === 'inactive';
  const params = codec?.parameters ?? {};
  const fmtp = Object.entries(params)
    .map(([k, v]) => `${k}=${v}`)
    .join(';');
  const exts = section.rtpParameters?.headerExtensions ?? [];
  const ssrc = section.rtpParameters?.encodings?.[0]?.ssrc;
  const cname = section.rtpParameters?.rtcp?.cname;
  const media: sdpTransform.MediaDescription = {
    type: video ? 'video' : 'audio',
    port: inactive && section.mid !== '0' ? 0 : port,
    protocol: 'UDP/TLS/RTP/SAVPF',
    payloads: String(pt),
    connection: { version: 4, ip: inactive && section.mid !== '0' ? '0.0.0.0' : ip },
    mid: section.mid,
    direction: inactive ? 'inactive' : section.direction,
    rtp: [
      video
        ? { payload: pt, codec: 'H264', rate: codec?.clockRate ?? 90000 }
        : { payload: pt, codec: 'opus', rate: codec?.clockRate ?? 48000, encoding: codec?.channels ?? 2 },
    ],
    rtcpFb: (codec?.rtcpFeedback ?? [{ type: 'transport-cc' }]).map((fb) => ({
      payload: pt,
      type: fb.type,
      ...(fb.parameter ? { subtype: fb.parameter } : {}),
    })),
    ext: exts.map((e) => ({ value: e.id, uri: e.uri })),
    fmtp: fmtp ? [{ payload: pt, config: fmtp }] : [],
    rtcpMux: 'rtcp-mux',
    rtcpRsize: 'rtcp-rsize',
  };
  if (!inactive || section.mid === '0') {
    const fp = transport.dtlsParameters.fingerprints.at(-1)!;
    media.iceUfrag = transport.iceParameters.usernameFragment;
    media.icePwd = transport.iceParameters.password;
    media.iceOptions = 'ice2';
    media.fingerprint = { type: fp.algorithm, hash: fp.value };
    media.setup = setup;
    media.rtcp = { port, netType: 'IN', ipVer: 4, address: ip };
    media.candidates = transport.iceCandidates.map((c) => ({
      foundation: c.foundation,
      component: 1,
      transport: c.protocol.toUpperCase(),
      priority: c.priority,
      ip: c.ip,
      port: c.port,
      type: c.type,
      tcptype: c.tcpType,
    }));
    media.endOfCandidates = 'end-of-candidates';
  }
  if (offering && ssrc && cname && !inactive) {
    const msid = `${section.streamId ?? 'talk'} ${section.trackId ?? section.mid}`;
    media.msid = msid;
    media.ssrcs = [
      { id: ssrc, attribute: 'cname', value: cname },
      { id: ssrc, attribute: 'msid', value: msid },
    ];
  }
  return media;
}

export function buildSdp(opts: {
  transport: TransportView;
  sections: SdpSection[];
  /** answer 固定 passive（远端 DTLS client）；recv offer 用 actpass */
  setup: 'passive' | 'active' | 'actpass';
  offering: boolean;
  sessionId: string;
  sessionVersion: number;
}): string {
  const udp = opts.transport.iceCandidates.find((c) => c.protocol === 'udp') ?? opts.transport.iceCandidates[0];
  if (!udp) throw new TalkError(500, 'no ice candidate');
  const ip = udp.ip;
  const bundle = opts.sections
    .filter((s) => !(s.closed || s.direction === 'inactive') || s.mid === '0')
    .map((s) => s.mid)
    .join(' ');
  const fp = opts.transport.dtlsParameters.fingerprints.at(-1)!;
  const session: sdpTransform.SessionDescription = {
    version: 0,
    origin: {
      username: 'talk',
      sessionId: opts.sessionId,
      sessionVersion: opts.sessionVersion,
      netType: 'IN',
      ipVer: 4,
      address: ip,
    },
    name: '-',
    timing: { start: 0, stop: 0 },
    icelite: opts.transport.iceParameters.iceLite ? 'ice-lite' : undefined,
    msidSemantic: { semantic: 'WMS', token: '*' },
    extmapAllowMixed: 'extmap-allow-mixed',
    groups: [{ type: 'BUNDLE', mids: bundle || '0' }],
    fingerprint: { type: fp.algorithm, hash: fp.value },
    media: opts.sections.map((s) => mediaFromSection(s, opts.transport, ip, udp.port, opts.setup, opts.offering)),
  };
  return sdpTransform.write(session);
}
