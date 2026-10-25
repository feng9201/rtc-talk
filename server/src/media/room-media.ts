import type { types as ms } from 'mediasoup';
import { TalkError } from '../gateway/protocol.js';
import type { Logger } from '../logger.js';
import { AUDIO_EXTENSIONS, VIDEO_EXTENSIONS } from './codecs.js';
import { buildSdp, parseSdp, type ParsedTrack, type SdpSection } from './sdp.js';

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

/** media.producer 通知的载荷：producer 的媒体事实 */
export interface ProducerState {
  mid: string;
  kind: 'audio' | 'video';
  source: 'mic' | 'camera' | 'screen';
  paused: boolean;
}

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

/** 参数对象按 key 排序后比较，避免字段顺序不同导致误判 */
function sameParams(a?: Record<string, unknown>, b?: Record<string, unknown>): boolean {
  const norm = (p?: Record<string, unknown>) =>
    JSON.stringify(Object.entries(p ?? {}).sort(([x], [y]) => x.localeCompare(y)));
  return norm(a) === norm(b);
}

/**
 * 同 mid 重发布（ICE restart、重发完整 offer）能否沿用原 producer。
 * 编码/PT/SSRC/fmtp 任一变化都要重建，否则客户端与 producer 的 RTP 对不上。
 */
function canReuse(producer: ms.Producer, rtp: ms.RtpParameters, source: 'mic' | 'camera' | 'screen'): boolean {
  if (producer.closed || trackSource(producer) !== source) return false;
  const current = producer.rtpParameters;
  const a = current.codecs[0];
  const b = rtp.codecs[0];
  if (!a || !b) return false;
  if (a.mimeType !== b.mimeType || a.payloadType !== b.payloadType) return false;
  if (!sameParams(a.parameters, b.parameters)) return false;
  return current.encodings?.[0]?.ssrc === rtp.encodings?.[0]?.ssrc;
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
  /** 屏幕共享是否已停（重发布同 mid 时沿用，不会因 ICE restart 意外恢复） */
  private screenMuted = false;
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
    /** 新 producer 创建后回调（RoomMedia 用来登记到音量/主讲人 observer） */
    private readonly onProducer?: (producer: ms.Producer) => void,
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
    videoPolicy?: { enabled: boolean; max_publishers: number },
    countRoomVideo?: (excludeMids: Set<string>) => number,
  ): Promise<{ sdp: string; producers: (ProducerState & { producer_id: string })[] }> {
    const parsed = parseSdp(sdp);
    this.checkVideoPolicy(parsed.tracks, tracks, videoPolicy, countRoomVideo);
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

    const produced: (ProducerState & { producer_id: string })[] = [];
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
      const existing = [...this.producers.values()].find((p) => p.appData.mid === track.mid && !p.closed);
      const paused = source === 'screen' ? this.screenMuted : source === 'camera' ? this.cameraMuted : this.muted;
      if (existing && canReuse(existing, track.rtpParameters, source)) {
        // 同一路重发布：沿用 producer，只对齐暂停状态，避免 ICE restart 丢首帧
        if (existing.paused !== paused) await (paused ? existing.pause() : existing.resume());
        produced.push({ mid: track.mid, producer_id: existing.id, kind: track.kind, source, paused: existing.paused });
        sections.push({
          mid: track.mid,
          kind: track.kind,
          closed: false,
          direction: 'recvonly',
          rtpParameters: existing.rtpParameters,
        });
        continue;
      }
      if (existing) existing.close();
      const producer = await this.send.produce({
        kind: track.kind,
        rtpParameters: track.rtpParameters,
        paused,
        appData: { uid: this.uid, mid: track.mid, source },
      });
      this.producers.set(producer.id, producer);
      this.watchProducer(producer);
      this.onProducer?.(producer);
      produced.push({ mid: track.mid, producer_id: producer.id, kind: track.kind, source, paused: producer.paused });
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

  /** 房间视频策略：enabled=false 拒绝视频轨；max_publishers 按房间内视频 producer 数计；同一 uid 每种 source 最多 1 路。 */
  private checkVideoPolicy(
    parsedTracks: ParsedTrack[],
    tracks?: { mid: string; kind: 'audio' | 'video'; source?: 'mic' | 'camera' | 'screen' }[],
    videoPolicy?: { enabled: boolean; max_publishers: number },
    countRoomVideo?: (excludeMids: Set<string>) => number,
  ): void {
    if (!videoPolicy) return;
    const offerMids = new Set(parsedTracks.map((t) => t.mid));
    const newVideo = parsedTracks.filter((t) => t.kind === 'video' && !t.closed);
    if (newVideo.length === 0) return;
    if (!videoPolicy.enabled) throw new TalkError(409, 'video disabled by room policy');
    const sources = new Set<string>();
    for (const track of newVideo) {
      const declared = tracks?.find((t) => t.mid === track.mid)?.source;
      const source = declared === 'screen' ? 'screen' : 'camera';
      if (sources.has(source)) throw new TalkError(409, `duplicate ${source} video track`);
      sources.add(source);
    }
    // 本次 offer 未覆盖的本端存量视频轨，source 不能与新轨重复（停掉的旧路不占名额）
    for (const p of this.producers.values()) {
      if (p.closed || p.paused || p.kind !== 'video') continue;
      if (offerMids.has(String(p.appData.mid))) continue;
      const source = p.appData.source === 'screen' ? 'screen' : 'camera';
      if (sources.has(source)) throw new TalkError(409, `duplicate ${source} video track`);
    }
    if (countRoomVideo && countRoomVideo(offerMids) + newVideo.length > videoPolicy.max_publishers) {
      throw new TalkError(409, 'video publisher limit reached');
    }
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

  /** 按 source 精确暂停/恢复：mic→音频，camera→摄像头，screen→屏幕共享。返回受影响的 producer */
  async setMuted(source: 'mic' | 'camera' | 'screen', muted: boolean): Promise<ProducerState[]> {
    if (source === 'camera') this.cameraMuted = muted;
    else if (source === 'screen') this.screenMuted = muted;
    else this.muted = muted;
    const targets = [...this.producers.values()].filter((p) => {
      if (p.closed) return false;
      if (source === 'mic') return p.kind === 'audio';
      return p.kind === 'video' && (p.appData.source === 'screen' ? 'screen' : 'camera') === source;
    });
    await Promise.all(targets.map((p) => (muted ? p.pause() : p.resume())));
    return targets.map((p) => ({
      mid: String(p.appData.mid ?? ''),
      kind: p.kind === 'video' ? 'video' : 'audio',
      source: trackSource(p),
      paused: p.paused,
    }));
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
  private activeObserver?: ms.ActiveSpeakerObserver;
  private levelObserver?: ms.AudioLevelObserver;
  /** 已登记到 observer 的 producer（避免重复 addProducer） */
  private readonly observed = new Set<string>();
  private enabling?: Promise<void>;
  /** 主讲人变化（uid），来自 ActiveSpeakerObserver */
  onActiveSpeaker?: (uid: string) => void;
  /** 音量（dBFS 归一化 0..1），observer 每 500ms 触发一次 */
  onAudioLevels?: (entries: { uid: string; level: number }[]) => void;

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
    const peer = new PeerMedia(uid, this.router, this.webRtcServer, this.log, this.answer, muted, videoOn, (p) =>
      this.watchProducer(p),
    );
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

  /** 建立主讲人 / 音量观测器；mediasoup 必须显式 addProducer 才会采样（与 Consumer 无关） */
  async enableObservers(): Promise<void> {
    if (this.activeObserver) return;
    if (!this.enabling) {
      this.enabling = this.openObservers().catch((err: unknown) => {
        this.enabling = undefined;
        throw err;
      });
    }
    await this.enabling;
  }

  private async openObservers(): Promise<void> {
    const active = await this.router.createActiveSpeakerObserver({ interval: 300 });
    const levels = await this.router.createAudioLevelObserver({ maxEntries: 24, threshold: -70, interval: 500 });

    active.on('dominantspeaker', ({ producer }) => {
      const uid = producer.appData.uid;
      if (typeof uid === 'string') this.onActiveSpeaker?.(uid);
    });
    levels.on('volumes', (volumes) => {
      const entries: { uid: string; level: number }[] = [];
      for (const { producer, volume } of volumes) {
        const uid = producer.appData.uid;
        if (typeof uid !== 'string') continue;
        // dBFS(-100..0) 归一化到 0..1，保留两位小数
        entries.push({ uid, level: Math.round(Math.max(0, Math.min(1, (volume + 100) / 100)) * 100) / 100 });
      }
      if (entries.length > 0) this.onAudioLevels?.(entries);
    });
    this.activeObserver = active;
    this.levelObserver = levels;
    // createRoom 不 await enableObservers：此处回补建立期间已发布的 producer
    for (const peer of this.peers.values()) {
      for (const producer of peer.producers.values()) this.watchProducer(producer);
    }
  }

  /** 音频 producer 登记到两个 observer；observer 未就绪或已登记时忽略 */
  private watchProducer(producer: ms.Producer): void {
    if (producer.closed || producer.kind !== 'audio') return;
    if (!this.activeObserver || !this.levelObserver || this.observed.has(producer.id)) return;
    this.observed.add(producer.id);
    producer.observer.on('close', () => this.observed.delete(producer.id));
    void this.activeObserver.addProducer({ producerId: producer.id }).catch((err: unknown) => {
      this.log.warn({ err, producer: producer.id }, 'active speaker addProducer failed');
    });
    void this.levelObserver.addProducer({ producerId: producer.id }).catch((err: unknown) => {
      this.log.warn({ err, producer: producer.id }, 'audio level addProducer failed');
    });
  }

  close(): void {
    this.activeObserver?.close();
    this.levelObserver?.close();
    this.observed.clear();
    this.enabling = undefined;
    for (const p of this.peers.values()) p.close();
    this.peers.clear();
    this.router.close();
  }
}
