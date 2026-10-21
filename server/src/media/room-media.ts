import type { types as ms } from 'mediasoup';
import { TalkError } from '../gateway/protocol.js';
import type { Logger } from '../logger.js';
import { AUDIO_EXTENSIONS, VIDEO_EXTENSIONS } from './codecs.js';
import { buildSdp, parseSdp, type SdpSection } from './sdp.js';

export interface RecvConsumerInfo {
  mid: string;
  consumer_id: string;
  producer_id: string;
  uid: string;
  kind: 'audio' | 'video';
  source: 'mic' | 'camera' | 'screen';
}

export interface RecvOffer {
  sdp: string;
  consumers: RecvConsumerInfo[];
  closed: string[];
}

export type AnswerHandler = (uid: string, offer: RecvOffer) => Promise<string>;

interface Slot {
  mid: string;
  kind: 'audio' | 'video';
  closed: boolean;
  consumer?: ms.Consumer;
  producerId?: string;
  uid?: string;
  source?: 'mic' | 'camera' | 'screen';
}

const COALESCE_MS = 100;

function recvCaps(router: ms.Router): ms.RtpCapabilities {
  return {
    codecs: router.rtpCapabilities.codecs?.filter((c) => {
      const mime = c.mimeType.toLowerCase();
      return (c.kind === 'audio' && mime === 'audio/opus') || (c.kind === 'video' && mime === 'video/h264');
    }),
    headerExtensions: router.rtpCapabilities.headerExtensions?.filter(
      (e) => (e.kind === 'audio' && AUDIO_EXTENSIONS.has(e.uri)) || (e.kind === 'video' && VIDEO_EXTENSIONS.has(e.uri)),
    ),
  };
}

function trackSource(producer: ms.Producer): 'mic' | 'camera' | 'screen' {
  const source = producer.appData.source;
  if (source === 'camera' || source === 'screen' || source === 'mic') return source;
  return producer.kind === 'video' ? 'camera' : 'mic';
}

export class PeerMedia {
  readonly producers = new Map<string, ms.Producer>();
  private send?: ms.WebRtcTransport;
  private recv?: ms.WebRtcTransport;
  private sendConnected = false;
  private recvConnected = false;
  /** 并发订阅音视频时只创建一次 recv transport */
  private openingRecv?: Promise<ms.WebRtcTransport>;
  private remoteIceUfrag = '';
  private pendingSendRestart = false;
  private readonly slots: Slot[] = [];
  private nextMid = 0;
  private sdpVersion = 0;
  private readonly sessionId = String(Math.floor(Math.random() * 1e9));
  private muted: boolean;
  private cameraMuted: boolean;
  private timer: NodeJS.Timeout | undefined;
  private busy = false;
  private dirty = false;
  private closed = false;
  needsFlush = false;

  constructor(
    readonly uid: string,
    private readonly router: ms.Router,
    private readonly webRtcServer: ms.WebRtcServer,
    private readonly log: Logger,
    private readonly answer: AnswerHandler,
    muted: boolean,
    videoOn = false,
  ) {
    this.muted = muted;
    this.cameraMuted = !videoOn;
  }

  get micProducer(): ms.Producer | undefined {
    return [...this.producers.values()].find((p) => p.kind === 'audio');
  }

  async publish(
    sdp: string,
    tracks?: { mid: string; kind: 'audio' | 'video'; source?: 'mic' | 'camera' | 'screen' }[],
  ): Promise<{ sdp: string; producers: { mid: string; producer_id: string; paused: boolean }[] }> {
    const parsed = parseSdp(sdp);
    if (!this.send) {
      this.send = await this.router.createWebRtcTransport({
        webRtcServer: this.webRtcServer,
        enableUdp: true,
        enableTcp: true,
        preferUdp: true,
        iceConsentTimeout: 30,
        appData: { uid: this.uid, dir: 'send' },
      });
      this.send.on('dtlsstatechange', (state) => {
        if (state === 'failed') this.log.warn({ uid: this.uid }, 'send dtls failed');
      });
    }
    if (!this.sendConnected) {
      await this.send.connect({ dtlsParameters: parsed.dtls });
      this.sendConnected = true;
      this.remoteIceUfrag = parsed.iceUfrag;
    } else if (this.pendingSendRestart || (parsed.iceUfrag && parsed.iceUfrag !== this.remoteIceUfrag)) {
      await this.send.restartIce();
      this.pendingSendRestart = false;
      this.remoteIceUfrag = parsed.iceUfrag;
    }

    const produced: { mid: string; producer_id: string; paused: boolean }[] = [];
    const sections: SdpSection[] = [];
    for (const track of parsed.tracks) {
      if (track.closed || !track.rtpParameters) {
        const existing = [...this.producers.entries()].find(([, p]) => p.appData.mid === track.mid);
        if (existing) {
          existing[1].close();
          this.producers.delete(existing[0]);
        }
        sections.push({ mid: track.mid, kind: track.kind, closed: true, direction: 'inactive' });
        continue;
      }
      const declared = tracks?.find((t) => t.mid === track.mid)?.source;
      const source = track.kind === 'video' ? (declared === 'screen' ? 'screen' : 'camera') : 'mic';
      let producer = [...this.producers.values()].find((p) => p.appData.mid === track.mid && !p.closed);
      if (producer) producer.close();
      producer = await this.send.produce({
        kind: track.kind,
        rtpParameters: track.rtpParameters,
        paused: source === 'screen' ? false : source === 'camera' ? this.cameraMuted : this.muted,
        appData: { uid: this.uid, mid: track.mid, source },
      });
      this.producers.set(producer.id, producer);
      this.watchProducer(producer);
      produced.push({ mid: track.mid, producer_id: producer.id, paused: producer.paused });
      sections.push({
        mid: track.mid,
        kind: track.kind,
        closed: false,
        direction: 'recvonly',
        rtpParameters: producer.rtpParameters,
      });
    }
    if (produced.length === 0) throw new TalkError(400, 'no track to publish');
    const answer = buildSdp({
      transport: this.send,
      sections,
      setup: 'passive',
      offering: false,
      sessionId: this.sessionId,
      sessionVersion: ++this.sdpVersion,
    });
    return { sdp: answer, producers: produced };
  }

  /** 其他成员发布后调用。短时间多次调用会合并成一次 sfu.offer。 */
  subscribe(producer: ms.Producer, fromUid: string): void {
    if (this.closed || fromUid === this.uid) return;
    if (this.slots.some((s) => s.producerId === producer.id && !s.closed)) return;
    void this.openConsumer(producer, fromUid).catch((err: unknown) => {
      this.log.warn({ err, uid: this.uid, producer: producer.id }, 'consume failed');
    });
  }

  unsubscribe(producerId: string): void {
    const slot = this.slots.find((s) => s.producerId === producerId && !s.closed);
    if (!slot) return;
    slot.consumer?.close();
    slot.consumer = undefined;
    slot.closed = true;
    slot.producerId = undefined;
    this.schedule();
  }

  async setMuted(source: 'mic' | 'camera', muted: boolean): Promise<void> {
    if (source === 'camera') this.cameraMuted = muted;
    else this.muted = muted;
    const targets = [...this.producers.values()].filter((p) => {
      if (p.closed) return false;
      if (source === 'mic') return p.kind === 'audio';
      return p.kind === 'video' && p.appData.source !== 'screen';
    });
    await Promise.all(targets.map((p) => (muted ? p.pause() : p.resume())));
  }

  async restartIce(pc: 'send' | 'recv'): Promise<void> {
    if (pc === 'send') {
      if (this.send) await this.send.restartIce();
      this.pendingSendRestart = true;
      return;
    }
    if (this.recv) await this.recv.restartIce();
    this.schedule();
  }

  flush(): void {
    if (this.slots.length > 0) this.schedule();
  }

  close(): void {
    this.closed = true;
    if (this.timer) clearTimeout(this.timer);
    for (const s of this.slots) s.consumer?.close();
    for (const p of this.producers.values()) p.close();
    this.producers.clear();
    this.send?.close();
    this.recv?.close();
  }

  private watchProducer(producer: ms.Producer): void {
    producer.observer.on('close', () => {
      this.producers.delete(producer.id);
    });
  }

  private async ensureRecv(): Promise<ms.WebRtcTransport> {
    if (this.recv) return this.recv;
    if (!this.openingRecv) {
      this.openingRecv = this.router.createWebRtcTransport({
        webRtcServer: this.webRtcServer,
        enableUdp: true,
        enableTcp: true,
        preferUdp: true,
        iceConsentTimeout: 30,
        appData: { uid: this.uid, dir: 'recv' },
      });
    }
    try {
      this.recv = await this.openingRecv;
    } catch (err) {
      this.openingRecv = undefined;
      throw err;
    }
    return this.recv;
  }

  private async openConsumer(producer: ms.Producer, fromUid: string): Promise<void> {
    if (this.closed || producer.closed) return;
    const recv = await this.ensureRecv();
    const slot = this.slots.find((s) => s.closed && s.kind === producer.kind);
    const mid = slot?.mid ?? String(this.nextMid++);
    const consumer = await recv.consume({
      producerId: producer.id,
      rtpCapabilities: recvCaps(this.router),
      paused: true,
      mid,
      appData: { uid: fromUid },
    });
    consumer.on('producerclose', () => {
      const current = this.slots.find((s) => s.consumer === consumer);
      if (!current || current.closed) return;
      current.closed = true;
      current.consumer = undefined;
      current.producerId = undefined;
      this.schedule();
    });
    const source = trackSource(producer);
    if (slot) {
      slot.closed = false;
      slot.consumer = consumer;
      slot.producerId = producer.id;
      slot.uid = fromUid;
      slot.source = source;
    } else {
      this.slots.push({
        mid,
        kind: producer.kind === 'video' ? 'video' : 'audio',
        closed: false,
        consumer,
        producerId: producer.id,
        uid: fromUid,
        source,
      });
    }
    this.schedule();
  }

  private schedule(): void {
    if (this.closed) return;
    this.dirty = true;
    if (this.busy || this.timer) return;
    this.timer = setTimeout(() => {
      this.timer = undefined;
      void this.drain();
    }, COALESCE_MS);
    this.timer.unref?.();
  }

  private async drain(): Promise<void> {
    if (this.busy || this.closed) return;
    this.busy = true;
    try {
      while (this.dirty && !this.closed) {
        this.dirty = false;
        await this.negotiateOnce();
      }
    } catch (err) {
      this.log.warn({ err, uid: this.uid }, 'recv negotiate failed');
      this.needsFlush = true;
    } finally {
      this.busy = false;
      if (this.dirty && !this.closed) this.schedule();
    }
  }

  private async negotiateOnce(): Promise<void> {
    if (!this.recv || this.slots.length === 0) return;
    const sections: SdpSection[] = this.slots.map((s) => ({
      mid: s.mid,
      kind: s.kind,
      closed: s.closed || !s.consumer,
      direction: s.closed || !s.consumer ? 'inactive' : 'sendonly',
      rtpParameters: s.consumer?.rtpParameters,
      streamId: s.uid,
      trackId: s.producerId,
    }));
    const offer = buildSdp({
      transport: this.recv,
      sections,
      setup: 'actpass',
      offering: true,
      sessionId: this.sessionId,
      sessionVersion: ++this.sdpVersion,
    });
    const consumers: RecvConsumerInfo[] = this.slots
      .filter((s) => !s.closed && s.consumer && s.uid && s.producerId)
      .map((s) => ({
        mid: s.mid,
        consumer_id: s.consumer!.id,
        producer_id: s.producerId!,
        uid: s.uid!,
        kind: s.kind,
        source: s.source ?? (s.kind === 'video' ? 'camera' : 'mic'),
      }));
    const closed = this.slots.filter((s) => s.closed).map((s) => s.mid);
    let answerSdp: string;
    try {
      answerSdp = await this.answer(this.uid, { sdp: offer, consumers, closed });
    } catch (err) {
      this.needsFlush = true;
      this.log.info({ err: (err as Error).message, uid: this.uid }, 'recv offer not answered');
      return;
    }
    const parsed = parseSdp(answerSdp);
    if (!this.recvConnected) {
      await this.recv.connect({ dtlsParameters: parsed.dtls });
      this.recvConnected = true;
    }
    await Promise.all(this.slots.filter((s) => s.consumer && !s.closed).map((s) => s.consumer!.resume()));
  }
}

export class RoomMedia {
  readonly peers = new Map<string, PeerMedia>();

  constructor(
    readonly id: string,
    readonly router: ms.Router,
    private readonly webRtcServer: ms.WebRtcServer,
    private readonly log: Logger,
    private readonly answer: AnswerHandler,
  ) {}

  addPeer(uid: string, muted: boolean, videoOn = false): PeerMedia {
    const existing = this.peers.get(uid);
    if (existing) return existing;
    const peer = new PeerMedia(uid, this.router, this.webRtcServer, this.log, this.answer, muted, videoOn);
    this.peers.set(uid, peer);
    for (const other of this.peers.values()) {
      if (other === peer) continue;
      for (const producer of other.producers.values()) {
        if (!producer.closed) peer.subscribe(producer, other.uid);
      }
    }
    return peer;
  }

  removePeer(uid: string): void {
    const peer = this.peers.get(uid);
    if (!peer) return;
    const producerIds = [...peer.producers.keys()];
    peer.close();
    this.peers.delete(uid);
    for (const id of producerIds) {
      for (const other of this.peers.values()) other.unsubscribe(id);
    }
  }

  close(): void {
    for (const p of this.peers.values()) p.close();
    this.peers.clear();
    this.router.close();
  }
}
