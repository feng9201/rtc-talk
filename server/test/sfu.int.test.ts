import type { AddressInfo } from 'node:net';
import { createServer } from 'node:net';
import { afterEach, describe, expect, it } from 'vitest';
import { MediaStreamTrack, RTCPeerConnection, RtpHeader, RtpPacket, useH264, useOPUS } from 'werift';
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
  recvVideo = 0;
  lastConsumers: { kind?: string; source?: string }[] = [];
  private sendPc?: RTCPeerConnection;
  private recvPc?: RTCPeerConnection;
  private track?: MediaStreamTrack;
  private videoTrack?: MediaStreamTrack;
  private pump?: NodeJS.Timeout;
  private videoPump?: NodeJS.Timeout;
  recvReady = Promise.resolve();
  private markReady = () => {};

  constructor(
    readonly wire: Wire,
    private readonly video = false,
  ) {
    this.recvReady = new Promise((r) => {
      this.markReady = () => r();
    });
    wire.onServerReq(async (method, data) => {
      if (method !== 'sfu.offer') return {};
      this.lastConsumers = (data.consumers ?? []) as { kind?: string; source?: string }[];
      return this.answer(data.sdp as string);
    });
  }

  private pcConfig() {
    return {
      iceServers: [] as [],
      iceAdditionalHostAddresses: ['127.0.0.1'],
      iceUseTcp: false,
      codecs: { audio: [useOPUS()], ...(this.video ? { video: [useH264()] } : {}) },
    };
  }

  async publish(): Promise<string> {
    const pc = new RTCPeerConnection(this.pcConfig());
    this.sendPc = pc;
    this.track = new MediaStreamTrack({ kind: 'audio' });
    pc.addTrack(this.track);
    const tracks: { mid: string; kind: 'audio' | 'video'; source: string }[] = [
      { mid: '0', kind: 'audio', source: 'mic' },
    ];
    if (this.video) {
      this.videoTrack = new MediaStreamTrack({ kind: 'video' });
      pc.addTrack(this.videoTrack);
      tracks.push({ mid: '1', kind: 'video', source: 'camera' });
    }
    const offer = await pc.createOffer();
    await pc.setLocalDescription(offer);
    const res = await this.wire.req('sfu.publish', { sdp: pc.localDescription!.sdp, tracks });
    if (!res.ok) throw new Error(`publish failed ${res.error?.code} ${res.error?.msg}`);
    await pc.setRemoteDescription({ type: 'answer', sdp: res.data.sdp });
    return res.data.sdp as string;
  }

  private async answer(sdp: string) {
    const pc = this.recvPc ?? new RTCPeerConnection(this.pcConfig());
    this.recvPc = pc;
    pc.onTrack.subscribe((track) => {
      track.onReceiveRtp.subscribe(() => {
        if (track.kind === 'video') this.recvVideo += 1;
        else this.recvCount += 1;
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

  startVideo(): void {
    const sdp = this.sendPc?.localDescription?.sdp ?? '';
    const video = sdp.split('m=video')[1] ?? '';
    const pt = Number(/a=rtpmap:(\d+) H264/i.exec(video)?.[1] ?? 102);
    const ssrc = Number(/a=ssrc:(\d+)/.exec(video)?.[1]);
    let seq = 1;
    let ts = 0;
    this.videoPump = setInterval(() => {
      const header = new RtpHeader({ payloadType: pt, sequenceNumber: seq++, timestamp: ts, ssrc, marker: true });
      ts += 3000;
      // STAP-A：SPS + PPS + IDR，mediasoup 只在识别到关键帧后才转发视频
      const sps = Buffer.from([
        0x67, 0x42, 0xc0, 0x1f, 0xda, 0x01, 0x40, 0x16, 0xec, 0x04, 0x40, 0x00, 0x00, 0x03, 0x00, 0x40, 0x00, 0x00,
        0x0c, 0x83, 0xc6, 0x0c, 0x44, 0x80,
      ]);
      const pps = Buffer.from([0x68, 0xce, 0x3c, 0x80]);
      const idr = Buffer.from([0x65, 0x88, 0x84, 0x00, 0x00]);
      const stap = Buffer.concat([
        Buffer.from([0x78]),
        Buffer.from([(sps.length >> 8) & 0xff, sps.length & 0xff]),
        sps,
        Buffer.from([(pps.length >> 8) & 0xff, pps.length & 0xff]),
        pps,
        Buffer.from([(idr.length >> 8) & 0xff, idr.length & 0xff]),
        idr,
      ]);
      this.videoTrack?.writeRtp(new RtpPacket(header, stap));
    }, 33);
  }

  stop(): void {
    if (this.pump) clearInterval(this.pump);
    if (this.videoPump) clearInterval(this.videoPump);
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

  it('two peers exchange single-layer h264', async () => {
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
      const peer = new Peer(wire, true);
      peers.push(peer);
      expect((await wire.req('session.hello', { client: { platform: 'linux', version: '0' } })).ok).toBe(true);
      expect(
        (
          await wire.req('room.join', {
            room_id: 'sfu-v',
            media: { audio: { muted: false }, video: { send: true } },
            policy: { video: { enabled: true } },
          })
        ).ok,
      ).toBe(true);
      return peer;
    }

    const a = await open('alice');
    const b = await open('bob');
    const answer = await a.publish();
    expect(answer).toMatch(/H264\/90000/);
    expect(answer).not.toMatch(/H265/i);
    await b.publish();
    await Promise.all([a.recvReady, b.recvReady]);
    expect(a.lastConsumers).toEqual(
      expect.arrayContaining([expect.objectContaining({ kind: 'video', source: 'camera' })]),
    );
    expect(b.lastConsumers).toEqual(
      expect.arrayContaining([expect.objectContaining({ kind: 'video', source: 'camera' })]),
    );
    a.startAudio();
    b.startAudio();
    a.startVideo();
    b.startVideo();
    await new Promise((r) => setTimeout(r, 1000));
    expect(a.recvCount).toBeGreaterThan(5);
    expect(b.recvCount).toBeGreaterThan(5);
    expect(a.recvVideo).toBeGreaterThan(5);
    expect(b.recvVideo).toBeGreaterThan(5);
  }, 20000);

  it('enforces room video policy (disabled / publisher limit / duplicate source)', async () => {
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

    async function open(uid: string, roomId: string, policy?: unknown, video = true): Promise<Peer> {
      const token = ((await (await fetch(`${base}/dev/token?uid=${uid}`)).json()) as { token: string }).token;
      const wire = await connect(base, token);
      const peer = new Peer(wire, video);
      peers.push(peer);
      expect((await wire.req('session.hello', { client: { platform: 'linux', version: '0' } })).ok).toBe(true);
      expect(
        (
          await wire.req('room.join', {
            room_id: roomId,
            media: { audio: { muted: false }, video: { send: true } },
            ...(policy ? { policy } : {}),
          })
        ).ok,
      ).toBe(true);
      return peer;
    }

    // enabled=false：视频轨被拒，纯音频可发布
    const v = await open('v1', 'sfu-novideo', { video: { enabled: false } });
    await expect(v.publish()).rejects.toThrow(/publish failed 409/);
    const au = await open('a1', 'sfu-novideo', { video: { enabled: false } }, false);
    await au.publish();

    // max_publishers=2：第 3 路视频被拒；释放一路后可再发
    const limit = { video: { enabled: true, max_publishers: 2 } };
    const p1 = await open('l1', 'sfu-limit', limit);
    const p2 = await open('l2', 'sfu-limit', limit);
    const p3 = await open('l3', 'sfu-limit', limit);
    await p1.publish();
    await p2.publish();
    await expect(p3.publish()).rejects.toThrow(/publish failed 409/);
    await p2.wire.req('room.leave', { room_id: 'sfu-limit' });
    await p3.publish();

    // 默认策略允许视频；同一 uid 重复 source 被拒
    const dup = await open('dup', 'sfu-dup');
    const pc = new RTCPeerConnection({
      iceServers: [] as [],
      iceAdditionalHostAddresses: ['127.0.0.1'],
      iceUseTcp: false,
      codecs: { audio: [useOPUS()], video: [useH264()] },
    });
    pc.addTrack(new MediaStreamTrack({ kind: 'audio' }));
    pc.addTrack(new MediaStreamTrack({ kind: 'video' }));
    pc.addTrack(new MediaStreamTrack({ kind: 'video' }));
    const offer = await pc.createOffer();
    await pc.setLocalDescription(offer);
    const res = await dup.wire.req('sfu.publish', {
      sdp: pc.localDescription!.sdp,
      tracks: [
        { mid: '0', kind: 'audio', source: 'mic' },
        { mid: '1', kind: 'video', source: 'camera' },
        { mid: '2', kind: 'video', source: 'camera' },
      ],
    });
    pc.close();
    expect(res.ok).toBe(false);
    expect(res.error.code).toBe(409);
    expect(res.error.msg).toMatch(/duplicate/);

    // camera + screen 两路同 offer（Windows 客户端的发布形态）应成功：
    // 修复前数字/字符串 mid 错配导致两路都回退 camera 被误判 duplicate
    const pc2 = new RTCPeerConnection({
      iceServers: [] as [],
      iceAdditionalHostAddresses: ['127.0.0.1'],
      iceUseTcp: false,
      codecs: { audio: [useOPUS()], video: [useH264()] },
    });
    pc2.addTrack(new MediaStreamTrack({ kind: 'audio' }));
    pc2.addTrack(new MediaStreamTrack({ kind: 'video' }));
    pc2.addTrack(new MediaStreamTrack({ kind: 'video' }));
    const offer2 = await pc2.createOffer();
    await pc2.setLocalDescription(offer2);
    const ok2 = await dup.wire.req('sfu.publish', {
      sdp: pc2.localDescription!.sdp,
      tracks: [
        { mid: '0', kind: 'audio', source: 'mic' },
        { mid: '1', kind: 'video', source: 'camera' },
        { mid: '2', kind: 'video', source: 'screen' },
      ],
    });
    pc2.close();
    expect(ok2.ok).toBe(true);
    expect(ok2.data.producers).toHaveLength(3);
  }, 20000);

  it('pauses screen and releases publisher quota without touching camera', async () => {
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

    async function open(uid: string, video = true): Promise<Peer> {
      const token = ((await (await fetch(`${base}/dev/token?uid=${uid}`)).json()) as { token: string }).token;
      const wire = await connect(base, token);
      const peer = new Peer(wire, video);
      peers.push(peer);
      expect((await wire.req('session.hello', { client: { platform: 'linux', version: '0' } })).ok).toBe(true);
      expect(
        (
          await wire.req('room.join', {
            room_id: 'sfu-screen',
            media: { audio: { muted: false }, video: { send: true } },
            policy: { video: { enabled: true, max_publishers: 2 } },
          })
        ).ok,
      ).toBe(true);
      return peer;
    }

    const a = await open('s1');
    const b = await open('s2');

    // A 同时发 camera + screen，占满 2 个名额
    const pc = new RTCPeerConnection({
      iceServers: [] as [],
      iceAdditionalHostAddresses: ['127.0.0.1'],
      iceUseTcp: false,
      codecs: { audio: [useOPUS()], video: [useH264()] },
    });
    pc.addTrack(new MediaStreamTrack({ kind: 'audio' }));
    pc.addTrack(new MediaStreamTrack({ kind: 'video' }));
    pc.addTrack(new MediaStreamTrack({ kind: 'video' }));
    const offer = await pc.createOffer();
    await pc.setLocalDescription(offer);
    const offerSdp = pc.localDescription!.sdp;
    const pub = await a.wire.req('sfu.publish', {
      sdp: offerSdp,
      tracks: [
        { mid: '0', kind: 'audio', source: 'mic' },
        { mid: '1', kind: 'video', source: 'camera' },
        { mid: '2', kind: 'video', source: 'screen' },
      ],
    });
    pc.close();
    expect(pub.ok).toBe(true);

    // 名额已满：B 的视频被拒
    await expect(b.publish()).rejects.toThrow(/publish failed 409/);

    // A 停共享：B 收到 media.producer，名额释放后 B 可发布
    const paused = new Promise<Record<string, unknown>>((resolve) => {
      b.wire.ws.on('message', (raw) => {
        const msg = JSON.parse(raw.toString());
        if (msg.type === 'ntf' && msg.method === 'media.producer') resolve(msg.data as Record<string, unknown>);
      });
    });
    const mute = await a.wire.req('media.setMute', { source: 'screen', muted: true });
    expect(mute.ok).toBe(true);
    expect(mute.data.video).toMatchObject({ camera: true, screen: false, on: true });
    expect(await paused).toMatchObject({ uid: 's1', kind: 'video', source: 'screen', paused: true });

    // ICE restart / 重发完整 offer：三路 producer 全部复用，屏幕路保持暂停
    const again = await a.wire.req('sfu.publish', {
      sdp: offerSdp,
      tracks: [
        { mid: '0', kind: 'audio', source: 'mic' },
        { mid: '1', kind: 'video', source: 'camera' },
        { mid: '2', kind: 'video', source: 'screen' },
      ],
    });
    expect(again.ok).toBe(true);
    expect(again.data.producers.map((p: { producer_id: string }) => p.producer_id)).toEqual(
      pub.data.producers.map((p: { producer_id: string }) => p.producer_id),
    );
    expect(again.data.producers.find((p: { mid: string }) => p.mid === '2')?.paused).toBe(true);

    await b.publish();
  }, 20000);

  it('retries recv offer after a failed answer once the peer publishes', async () => {
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

    async function helloJoin(wire: Wire, _uid: string): Promise<void> {
      expect((await wire.req('session.hello', { client: { platform: 'linux', version: '0' } })).ok).toBe(true);
      expect((await wire.req('room.join', { room_id: 'sfu-retry', media: { audio: { muted: false } } })).ok).toBe(true);
    }

    // b 先入房但不挂 sfu.offer handler：a 发布时给 b 的 offer 会应答失败
    const bToken = ((await (await fetch(`${base}/dev/token?uid=bob`)).json()) as { token: string }).token;
    const bWire = await connect(base, bToken);
    await helloJoin(bWire, 'bob');
    const aToken = ((await (await fetch(`${base}/dev/token?uid=alice`)).json()) as { token: string }).token;
    const aWire = await connect(base, aToken);
    const a = new Peer(aWire);
    peers.push(a);
    await helloJoin(aWire, 'alice');
    await a.publish();
    await new Promise((r) => setTimeout(r, 300));

    // b 现在具备应答能力，再发布 → 服务端应重试此前失败的 recv offer
    let offers = 0;
    let lastConsumers: { uid?: string; kind?: string }[] = [];
    const bRecv = new RTCPeerConnection({
      iceServers: [] as [],
      iceAdditionalHostAddresses: ['127.0.0.1'],
      iceUseTcp: false,
      codecs: { audio: [useOPUS()] },
    });
    bWire.onServerReq(async (method, data) => {
      if (method !== 'sfu.offer') return {};
      offers += 1;
      lastConsumers = ((data as { consumers?: { uid?: string; kind?: string }[] }).consumers ?? []).slice();
      await bRecv.setRemoteDescription({ type: 'offer', sdp: (data as { sdp: string }).sdp });
      const answer = await bRecv.createAnswer();
      await bRecv.setLocalDescription(answer);
      return { sdp: bRecv.localDescription!.sdp };
    });
    const bSend = new RTCPeerConnection({
      iceServers: [] as [],
      iceAdditionalHostAddresses: ['127.0.0.1'],
      iceUseTcp: false,
      codecs: { audio: [useOPUS()] },
    });
    bSend.addTrack(new MediaStreamTrack({ kind: 'audio' }));
    const bOffer = await bSend.createOffer();
    await bSend.setLocalDescription(bOffer);
    const res = await bWire.req('sfu.publish', {
      sdp: bSend.localDescription!.sdp,
      tracks: [{ mid: '0', kind: 'audio', source: 'mic' }],
    });
    expect(res.ok).toBe(true);
    await bSend.setRemoteDescription({ type: 'answer', sdp: res.data.sdp });

    await new Promise((r) => setTimeout(r, 1000));
    expect(offers).toBeGreaterThanOrEqual(1);
    expect(lastConsumers).toEqual(expect.arrayContaining([expect.objectContaining({ uid: 'alice', kind: 'audio' })]));
    bRecv.close();
    bSend.close();
    bWire.close();
  }, 20000);

  it('grants ptt floor and resumes the paused producer', async () => {
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

    async function open(uid: string, policy?: object) {
      const token = ((await (await fetch(`${base}/dev/token?uid=${uid}`)).json()) as { token: string }).token;
      const wire = await connect(base, token);
      const peer = new Peer(wire);
      peers.push(peer);
      expect((await wire.req('session.hello', { client: { platform: 'linux', version: '0' } })).ok).toBe(true);
      const joined = await wire.req('room.join', {
        room_id: 'sfu-ptt',
        media: { audio: { muted: false } },
        ...(policy ? { policy } : {}),
      });
      expect(joined.ok).toBe(true);
      return { peer, wire, joined };
    }

    const a = await open('alice', { talk_mode: 'ptt' });
    const b = await open('bob');
    expect(a.joined.data.media_mode).toBe('sfu');

    // 入房即静音：发布后 audio producer 仍为 paused，双方都收不到
    await a.peer.publish();
    await b.peer.publish();
    await Promise.all([a.peer.recvReady, b.peer.recvReady]);
    a.peer.startAudio();
    b.peer.startAudio();
    await new Promise((r) => setTimeout(r, 500));
    expect(a.peer.recvCount).toBe(0);
    expect(b.peer.recvCount).toBe(0);

    // alice 拿到发言权 → producer resume，bob 立即能听到
    expect((await a.wire.req('floor.request', {})).data).toEqual({ granted: true });
    await new Promise((r) => setTimeout(r, 700));
    expect(b.peer.recvCount).toBeGreaterThan(3);
    expect(a.peer.recvCount).toBe(0);

    // bob 排队；alice 释放后递补，反向也通
    expect((await b.wire.req('floor.request', {})).data).toEqual({ granted: false, queue_pos: 1 });
    expect((await a.wire.req('floor.release', {})).ok).toBe(true);
    await new Promise((r) => setTimeout(r, 700));
    expect(a.peer.recvCount).toBeGreaterThan(3);
  }, 25000);

  it('rejects the second unmute when max_speakers is reached', async () => {
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

    async function open(uid: string, policy?: object) {
      const token = ((await (await fetch(`${base}/dev/token?uid=${uid}`)).json()) as { token: string }).token;
      const wire = await connect(base, token);
      const peer = new Peer(wire);
      peers.push(peer);
      expect((await wire.req('session.hello', { client: { platform: 'linux', version: '0' } })).ok).toBe(true);
      const joined = await wire.req('room.join', { room_id: 'sfu-max', ...(policy ? { policy } : {}) });
      expect(joined.ok).toBe(true);
      return { peer, wire };
    }

    const a = await open('alice', { max_speakers: 1 });
    const b = await open('bob');
    await a.peer.publish();
    await b.peer.publish();

    expect((await a.wire.req('media.setMute', { source: 'mic', muted: false })).ok).toBe(true);
    expect((await b.wire.req('media.setMute', { source: 'mic', muted: false })).error?.code).toBe(429);

    // 一个人关麦后名额释放
    expect((await a.wire.req('media.setMute', { source: 'mic', muted: true })).ok).toBe(true);
    expect((await b.wire.req('media.setMute', { source: 'mic', muted: false })).ok).toBe(true);
  }, 20000);
});
