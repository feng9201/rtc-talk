import type { AddressInfo } from 'node:net';
import { createServer } from 'node:net';
import { afterEach, describe, expect, it } from 'vitest';
import { MediaStreamTrack, RTCPeerConnection, RtpHeader, RtpPacket, useOPUS } from 'werift';
import { WebSocket } from 'ws';
import { createApp, type App } from '../src/app.js';
import { loadConfig } from '../src/config.js';
import { createLogger } from '../src/logger.js';
import { MediaNode } from '../src/media/media-node.js';
import { SUBPROTOCOL, WS_PATH } from '../src/gateway/protocol.js';

const logger = createLogger({ level: 'silent', pretty: false });

function freePort(): Promise<number> {
  return new Promise((resolve, reject) => {
    const srv = createServer();
    srv.once('error', reject);
    srv.listen(0, '127.0.0.1', () => {
      const port = (srv.address() as AddressInfo).port;
      srv.close(() => resolve(port));
    });
  });
}

interface Wire {
  ws: WebSocket;
  req(method: string, data?: unknown): Promise<any>;
  onServerReq(fn: (method: string, data: any) => Promise<unknown>): void;
  close(): void;
}

function connect(base: string, token: string): Promise<Wire> {
  const ws = new WebSocket(`${base.replace(/^http/, 'ws')}${WS_PATH}?token=${token}`, SUBPROTOCOL);
  let seq = 1;
  const pending = new Map<string, (msg: any) => void>();
  let onReq: (method: string, data: any) => Promise<unknown> = async () => ({});
  ws.on('message', (raw) => {
    const msg = JSON.parse(raw.toString());
    if (msg.type === 'res') pending.get(msg.id)?.(msg);
    else if (msg.type === 'req') {
      void onReq(msg.method, msg.data).then(
        (data) => ws.send(JSON.stringify({ v: 1, type: 'res', id: msg.id, ok: true, data: data ?? {} })),
        (err: Error) =>
          ws.send(
            JSON.stringify({
              v: 1,
              type: 'res',
              id: msg.id,
              ok: false,
              error: { code: 500, reason: 'internal', msg: err.message },
            }),
          ),
      );
    }
  });
  const wire: Wire = {
    ws,
    req(method, data = {}) {
      const id = `c-${seq++}`;
      return new Promise((resolve) => {
        pending.set(id, resolve);
        ws.send(JSON.stringify({ v: 1, type: 'req', id, method, data }));
      });
    },
    onServerReq(fn) {
      onReq = fn;
    },
    close() {
      ws.close();
    },
  };
  return new Promise((resolve, reject) => {
    ws.once('open', () => resolve(wire));
    ws.once('error', reject);
  });
}

class Peer {
  recvCount = 0;
  private sendPc?: RTCPeerConnection;
  private recvPc?: RTCPeerConnection;
  private track?: MediaStreamTrack;
  private pump?: NodeJS.Timeout;
  recvReady = Promise.resolve();
  private markReady = () => {};

  constructor(private readonly wire: Wire) {
    this.recvReady = new Promise((r) => {
      this.markReady = () => r();
    });
    wire.onServerReq(async (method, data) => {
      if (method !== 'sfu.offer') return {};
      return this.answer(data.sdp as string);
    });
  }

  private pcConfig() {
    return {
      iceServers: [] as [],
      iceAdditionalHostAddresses: ['127.0.0.1'],
      iceUseTcp: false,
      codecs: { audio: [useOPUS()] },
    };
  }

  async publish(): Promise<void> {
    const pc = new RTCPeerConnection(this.pcConfig());
    this.sendPc = pc;
    this.track = new MediaStreamTrack({ kind: 'audio' });
    pc.addTrack(this.track);
    const offer = await pc.createOffer();
    await pc.setLocalDescription(offer);
    const res = await this.wire.req('sfu.publish', {
      sdp: pc.localDescription!.sdp,
      tracks: [{ mid: '0', kind: 'audio', source: 'mic' }],
    });
    if (!res.ok) throw new Error(`publish failed ${res.error?.code} ${res.error?.msg}`);
    await pc.setRemoteDescription({ type: 'answer', sdp: res.data.sdp });
  }

  private async answer(sdp: string) {
    const pc = this.recvPc ?? new RTCPeerConnection(this.pcConfig());
    this.recvPc = pc;
    pc.onTrack.subscribe((track) => {
      track.onReceiveRtp.subscribe(() => {
        this.recvCount += 1;
      });
    });
    await pc.setRemoteDescription({ type: 'offer', sdp });
    const answer = await pc.createAnswer();
    await pc.setLocalDescription(answer);
    this.markReady();
    return { sdp: pc.localDescription!.sdp };
  }

  startAudio(): void {
    const sdp = this.sendPc?.localDescription?.sdp ?? '';
    const pt = Number(/a=rtpmap:(\d+) opus/i.exec(sdp)?.[1] ?? 111);
    const ssrc = Number(/a=ssrc:(\d+)/.exec(sdp)?.[1]);
    let seq = 1;
    let ts = 0;
    this.pump = setInterval(() => {
      const header = new RtpHeader({ payloadType: pt, sequenceNumber: seq++, timestamp: ts, ssrc, marker: false });
      ts += 960;
      this.track?.writeRtp(new RtpPacket(header, Buffer.from([0xf8, 0xff, 0xfe])));
    }, 20);
  }

  stop(): void {
    if (this.pump) clearInterval(this.pump);
    this.sendPc?.close();
    this.recvPc?.close();
    this.wire.close();
  }
}

describe('sfu audio', () => {
  let app: App | undefined;
  let media: MediaNode | undefined;
  const peers: Peer[] = [];

  afterEach(() => {
    for (const p of peers.splice(0)) p.stop();
    app?.close();
    media?.close();
  });

  it('two peers exchange opus rtp', async () => {
    const rtcPort = await freePort();
    media = await MediaNode.create({
      numWorkers: 1,
      workerBin: process.env.MEDIASOUP_WORKER_BIN,
      listenIp: '127.0.0.1',
      announcedAddress: '127.0.0.1',
      port: rtcPort,
      logger,
      onWorkerDied: () => {},
    });
    const config = loadConfig({
      TALK_ENV: 'dev',
      TALK_HTTP_HOST: '127.0.0.1',
      TALK_JWT_SECRET: 's'.repeat(40),
    });
    config.http.port = 0;
    app = createApp({ config, logger, media });
    const addr = await app.listen();
    const base = `http://127.0.0.1:${addr.port}`;

    async function open(uid: string): Promise<Peer> {
      const token = ((await (await fetch(`${base}/dev/token?uid=${uid}`)).json()) as { token: string }).token;
      const wire = await connect(base, token);
      const peer = new Peer(wire);
      peers.push(peer);
      expect((await wire.req('session.hello', { client: { platform: 'linux', version: '0' } })).ok).toBe(true);
      expect((await wire.req('room.join', { room_id: 'sfu', media: { audio: { muted: false } } })).ok).toBe(true);
      return peer;
    }

    const a = await open('alice');
    const b = await open('bob');
    await a.publish();
    await b.publish();
    await Promise.all([a.recvReady, b.recvReady]);
    a.startAudio();
    b.startAudio();
    await new Promise((r) => setTimeout(r, 800));
    expect(a.recvCount).toBeGreaterThan(5);
    expect(b.recvCount).toBeGreaterThan(5);
  }, 20000);
});
