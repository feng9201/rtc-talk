import { MediaStreamTrack, RTCPeerConnection, RtpHeader, RtpPacket, useOPUS } from 'werift';
import type { SignalClient } from './signal-client.js';

/** werift 的 offer 不带 extmap；补一条 RFC 6464 音量扩展，让 SFU 能算出音量 */
function withAudioLevelExt(sdp: string): string {
  return sdp.replace(/(m=audio [^\r\n]*\r?\n)/, '$1a=extmap:1 urn:ietf:params:rtp-hdrext:ssrc-audio-level\r\n');
}

/** 用 werift（标准 SDP）向 SFU 发 Opus，并统计收到的 RTP 包 */
export class AudioSender {
  received = 0;
  private sendPc?: RTCPeerConnection;
  private recvPc?: RTCPeerConnection;
  private track?: MediaStreamTrack;
  private pump?: NodeJS.Timeout;
  private ready!: () => void;
  readonly subscribed: Promise<void>;

  constructor(
    private client: SignalClient,
    private readonly loopback: boolean,
  ) {
    this.subscribed = new Promise((r) => {
      this.ready = () => r();
    });
    client.handleRequest(async (method, data) => {
      if (method !== 'sfu.offer') return {};
      return this.answer(String(data.sdp ?? ''));
    });
  }

  /** 断线重连后信令客户端换了，接收 offer 要重新挂上 */
  rebind(client: SignalClient): void {
    this.client = client;
    client.handleRequest(async (method, data) => {
      if (method !== 'sfu.offer') return {};
      return this.answer(String(data.sdp ?? ''));
    });
  }

  private pcConfig() {
    return {
      iceServers: [] as [],
      iceUseTcp: false,
      codecs: { audio: [useOPUS()] },
      ...(this.loopback ? { iceAdditionalHostAddresses: ['127.0.0.1'] } : {}),
    };
  }

  async publish(): Promise<void> {
    const pc = new RTCPeerConnection(this.pcConfig());
    this.sendPc = pc;
    this.track = new MediaStreamTrack({ kind: 'audio' });
    pc.addTrack(this.track);
    const offer = await pc.createOffer();
    await pc.setLocalDescription(offer);
    const answer = await this.client.request<{ sdp: string }>('sfu.publish', {
      sdp: withAudioLevelExt(pc.localDescription?.sdp ?? ''),
      tracks: [{ mid: '0', kind: 'audio', source: 'mic' }],
    });
    await pc.setRemoteDescription({ type: 'answer', sdp: answer.sdp });
  }

  private async answer(sdp: string) {
    const pc = this.recvPc ?? new RTCPeerConnection(this.pcConfig());
    this.recvPc = pc;
    pc.onTrack.subscribe((track) => {
      track.onReceiveRtp.subscribe(() => {
        this.received += 1;
      });
    });
    await pc.setRemoteDescription({ type: 'offer', sdp });
    const local = await pc.createAnswer();
    await pc.setLocalDescription(local);
    this.ready();
    return { sdp: pc.localDescription?.sdp };
  }

  start(): void {
    const sdp = this.sendPc?.localDescription?.sdp ?? '';
    const pt = Number(/a=rtpmap:(\d+) opus/i.exec(sdp)?.[1] ?? 111);
    const ssrc = Number(/a=ssrc:(\d+)/.exec(sdp)?.[1]);
    let seq = 1;
    let ts = 0;
    this.pump = setInterval(() => {
      this.track?.writeRtp(
        new RtpPacket(
          new RtpHeader({
            payloadType: pt,
            sequenceNumber: seq++,
            timestamp: ts,
            ssrc,
            // RFC 6464 音量扩展（V=1 + level=10）：没有它 mediasoup 的 AudioLevelObserver 读不到音量
            extensions: [{ id: 1, payload: Buffer.from([0x8a]) }],
          }),
          Buffer.from([0xf8, 0xff, 0xfe]),
        ),
      );
      ts += 960;
    }, 20);
  }

  stop(): void {
    if (this.pump) clearInterval(this.pump);
    this.sendPc?.close();
    this.recvPc?.close();
  }
}
