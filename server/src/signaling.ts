import { z } from 'zod';
import { buildIceServers, type IceConfig } from './auth/ice.js';
import { CallManager } from './call/call-manager.js';
import { RoomMode, type ModeView } from './call/room-mode.js';
import { CloseCode, type Connection, type ConnectionHandlers } from './gateway/connection.js';
import { parseData, TalkError, type NotifyMessage, type RequestMessage } from './gateway/protocol.js';
import type { Logger } from './logger.js';
import type { MediaNode } from './media/media-node.js';
import { SFU_VIDEO_CODECS } from './media/codecs.js';
import type { RecvOffer } from './media/room-media.js';
import { Floor } from './room/floor.js';
import { PolicyInputSchema } from './room/policy.js';
import { RoomManager, type LeaveReason, type Room, type RoomEvent } from './room/room-manager.js';
import { SessionManager, type DestroyReason, type Session } from './session/session-manager.js';

export interface SignalingOptions {
  logger: Logger;
  ice: IceConfig;
  heartbeatInterval: number;
  resumeTtlMs: number;
  maxPeersLimit: number;
  ringTimeoutMs: number;
  p2pTimeoutMs: number;
  media?: MediaNode;
}

const HelloSchema = z.object({
  client: z.object({
    platform: z.string().min(1).max(32),
    version: z.string().min(1).max(32),
    device_id: z.string().max(128).optional(),
    sdk: z.string().max(64).optional(),
  }),
  caps: z.unknown().optional(),
  resume: z.object({ session_id: z.string().max(64) }).optional(),
});

/** 只取决策用到的字段，未知字段忽略；解析失败按空 caps 处理，不拒绝 hello */
const CapsSchema = z
  .object({
    p2p: z.boolean().optional(),
    p2p_video: z.boolean().optional(),
    video: z
      .object({ codecs: z.array(z.object({ name: z.string().max(16), hw: z.boolean().optional() })).optional() })
      .passthrough()
      .optional(),
  })
  .passthrough();

const JoinSchema = z.object({
  room_id: z.string(),
  display: z.string().max(64).optional(),
  media: z
    .object({
      audio: z
        .object({ send: z.boolean().default(true), muted: z.boolean().default(true) })
        .partial()
        .optional(),
      video: z
        .object({ send: z.boolean().default(false) })
        .partial()
        .optional(),
    })
    .optional(),
  policy: PolicyInputSchema.optional(),
});

const LeaveSchema = z.object({ room_id: z.string() });
const SetMuteSchema = z.object({ source: z.enum(['mic', 'camera', 'screen']), muted: z.boolean() });
const PublishSchema = z.object({
  sdp: z.string().min(1),
  tracks: z
    .array(
      z.object({
        mid: z.string(),
        kind: z.enum(['audio', 'video']),
        source: z.enum(['mic', 'camera', 'screen']).optional(),
        closed: z.boolean().optional(),
      }),
    )
    .optional(),
});
const RestartIceSchema = z.object({ pc: z.enum(['send', 'recv']) });
const HostGrantSchema = z.object({ uid: z.string().min(1).max(64), allow: z.boolean() });
const HostMuteAllSchema = z.object({ except: z.array(z.string().min(1).max(64)).max(64).optional() });
const HostKickSchema = z.object({ uid: z.string().min(1).max(64) });
const SubscribeLevelsSchema = z.object({ enabled: z.boolean() });
/** 客户端每 10s 上报的接收质量；loss 兼容 0~1 与 0~100 两种口径 */
const ClientStatsSchema = z.object({
  pc: z.enum(['send', 'recv']).optional(),
  rtt: z.number().finite().nonnegative().max(60_000).optional(),
  jitter: z.number().finite().nonnegative().max(60_000).optional(),
  loss: z.number().finite().min(0).max(100).optional(),
  bitrate: z.number().finite().nonnegative().max(1e9).optional(),
});

type Handler = (session: Session, data: unknown, conn: Connection) => unknown;

const DESTROY_TO_LEAVE: Record<DestroyReason, LeaveReason> = { bye: 'leave', timeout: 'timeout', replaced: 'replaced' };

const P2P_NOTIFY = new Set(['p2p.offer', 'p2p.answer', 'p2p.candidate', 'p2p.state']);

function supportsP2p(caps: unknown): boolean {
  return typeof caps === 'object' && caps !== null && (caps as { p2p?: unknown }).p2p === true;
}

function supportsP2pVideo(caps: unknown): boolean {
  return typeof caps === 'object' && caps !== null && (caps as { p2p_video?: unknown }).p2p_video === true;
}

export class Signaling {
  readonly sessions: SessionManager;
  readonly rooms: RoomManager;
  private readonly calls: CallManager;
  private readonly modes: RoomMode;
  private readonly floors: Floor;
  /** 开启了 room.subscribeLevels 的 uid（会话销毁时清理） */
  private readonly levelsSubscribers = new Set<string>();
  private readonly log: Logger;
  private readonly handlers: Record<string, Handler>;
  private readonly clientStats = {
    reports: 0,
    rttSum: 0,
    rttCount: 0,
    lossSum: 0,
    lossCount: 0,
    bitrateSum: 0,
    bitrateCount: 0,
  };

  constructor(private readonly opts: SignalingOptions) {
    this.log = opts.logger.child({ mod: 'signaling' });
    this.rooms = new RoomManager({ maxPeersLimit: opts.maxPeersLimit, onEvent: (ev) => this.onRoomEvent(ev) });
    this.sessions = new SessionManager({
      resumeTtlMs: opts.resumeTtlMs,
      onDestroy: (s, reason) => {
        this.log.info({ uid: s.uid, session: s.id, reason }, 'session destroyed');
        if (s.roomId) this.rooms.leave(s.roomId, s.uid, DESTROY_TO_LEAVE[reason]);
        s.roomId = null;
        this.levelsSubscribers.delete(s.uid);
        this.calls.onUserGone(s.uid);
      },
    });
    this.calls = new CallManager({
      logger: opts.logger,
      ringTimeoutMs: opts.ringTimeoutMs,
      lookup: (uid) => {
        const s = this.sessions.ofUid(uid);
        if (!s) return undefined;
        return { online: Boolean(s.conn?.isOpen), roomId: s.roomId };
      },
      roomMembers: (roomId) => this.rooms.get(roomId)?.members.size ?? 0,
      notify: (uid, method, data) => this.notify(uid, method, data),
    });
    this.modes = new RoomMode({
      logger: opts.logger,
      p2pTimeoutMs: opts.p2pTimeoutMs,
      room: (roomId) => this.rooms.get(roomId),
      supportsP2p: (uid) => supportsP2p(this.sessions.ofUid(uid)?.caps),
      supportsP2pVideo: (uid) => supportsP2pVideo(this.sessions.ofUid(uid)?.caps),
      callerOf: (roomId) => this.calls.callerOf(roomId),
      online: (uid) => Boolean(this.sessions.ofUid(uid)?.conn?.isOpen),
      request: (uid, method, data) => this.request(uid, method, data),
      notify: (uid, method, data) => this.notify(uid, method, data),
    });
    this.floors = new Floor({
      logger: opts.logger,
      policy: (roomId) => this.rooms.get(roomId)?.policy,
      members: (roomId) => [...(this.rooms.get(roomId)?.members.keys() ?? [])],
      isHost: (roomId, uid) => this.rooms.get(roomId)?.members.get(uid)?.role === 'host',
      setMuted: (roomId, uid, muted) => {
        this.rooms.setMuted(roomId, uid, muted);
      },
      applyMic: (roomId, uid, on) =>
        this.announceProducers(this.rooms.get(roomId), uid, () => this.opts.media?.setMuted(roomId, uid, !on, 'mic')),
      notify: (uid, method, data) => this.notify(uid, method, data),
      broadcast: (roomId, method, data) => {
        const room = this.rooms.get(roomId);
        if (room) this.broadcast(room, method, data);
      },
    });
    opts.media?.setAudioObservers({
      activeSpeaker: (roomId, uid) => this.onActiveSpeaker(roomId, uid),
      audioLevels: (roomId, entries) => this.onAudioLevels(roomId, entries),
    });
    this.handlers = {
      'session.ping': () => ({ server_time: Date.now() }),
      'session.refreshIce': (s) => ({ ice_servers: buildIceServers(this.opts.ice, s.uid) }),
      'session.bye': (s, _d, conn) => {
        this.sessions.destroy(s, 'bye');
        setImmediate(() => conn.close(CloseCode.normal, 'bye'));
        return {};
      },
      'room.join': (s, d) => this.join(s, d),
      'room.leave': (s, d) => this.leave(s, d),
      'host.setPolicy': (s, d) => this.setPolicy(s, d),
      'host.grant': (s, d) => this.hostGrant(s, d),
      'host.muteAll': (s, d) => this.hostMuteAll(s, d),
      'host.kick': (s, d) => this.hostKick(s, d),
      'floor.request': (s) => this.floorRequest(s),
      'floor.release': (s) => this.floorRelease(s),
      'room.subscribeLevels': (s, d) => this.subscribeLevels(s, d),
      'media.setMute': (s, d) => this.setMute(s, d),
      'call.invite': (s, d) => this.calls.invite(s, d),
      'call.ringing': (s, d) => this.calls.ringing(s.uid, d),
      'call.accept': (s, d) => this.calls.accept(s.uid, d),
      'call.reject': (s, d) => this.calls.reject(s.uid, d),
      'call.cancel': (s, d) => this.calls.cancel(s.uid, d),
      'call.hangup': (s, d) => this.hangup(s, d),
      'sfu.publish': (s, d) => this.publish(s, d),
      'sfu.restartIce': (s, d) => this.restartIce(s, d),
      'sfu.stats': (_s, d) => {
        this.recordStats(d);
        return {};
      },
    };
  }

  /** mediasoup 要给接收端发 offer 时回调 */
  async answerRecvOffer(uid: string, offer: RecvOffer): Promise<string> {
    const conn = this.sessions.ofUid(uid)?.conn;
    if (!conn?.isOpen) throw new Error('peer offline');
    const data = (await conn.request('sfu.offer', offer)) as { sdp?: string };
    if (!data.sdp) throw new TalkError(400, 'sfu.offer answer missing sdp');
    return data.sdp;
  }

  /** 交给 WsGateway 的连接回调 */
  get connectionHandlers(): ConnectionHandlers {
    return {
      onRequest: (conn, req) => this.onRequest(conn, req),
      onNotify: (conn, ntf) => this.onNotify(conn, ntf),
      onClose: (conn) => this.sessions.detach(conn),
    };
  }

  private async onRequest(conn: Connection, req: RequestMessage): Promise<unknown> {
    if (req.method === 'session.hello') return this.hello(conn, req.data);
    const session = this.sessions.ofConn(conn);
    if (!session) throw new TalkError(409, 'session.hello required');
    const handler = this.handlers[req.method];
    if (!handler) throw new TalkError(501, `method ${req.method} not implemented`);
    return handler(session, req.data, conn);
  }

  private onNotify(conn: Connection, ntf: NotifyMessage): void {
    const session = this.sessions.ofConn(conn);
    if (!session) {
      conn.log.debug({ method: ntf.method }, 'unhandled notify');
      return;
    }
    // 协议 §6.5：sfu.stats 是 notify，不占用 req/res
    if (ntf.method === 'sfu.stats') {
      this.recordStats(ntf.data);
      return;
    }
    if (session.roomId && P2P_NOTIFY.has(ntf.method)) {
      this.modes.onNotify(session.uid, session.roomId, ntf.method, ntf.data);
      return;
    }
    conn.log.debug({ method: ntf.method }, 'unhandled notify');
  }

  /** 客户端上报质量的滑动统计（不做高基数标签，只出平均值） */
  private recordStats(data: unknown): void {
    const parsed = ClientStatsSchema.safeParse(data);
    if (!parsed.success) return;
    const s = this.clientStats;
    s.reports++;
    if (parsed.data.rtt !== undefined) {
      s.rttSum += parsed.data.rtt;
      s.rttCount++;
    }
    if (parsed.data.loss !== undefined) {
      s.lossSum += parsed.data.loss > 1 ? parsed.data.loss / 100 : parsed.data.loss;
      s.lossCount++;
    }
    if (parsed.data.bitrate !== undefined) {
      s.bitrateSum += parsed.data.bitrate;
      s.bitrateCount++;
    }
  }

  /** /metrics 用 */
  statsSnapshot() {
    const s = this.clientStats;
    return {
      reports: s.reports,
      rtt_avg_ms: s.rttCount > 0 ? s.rttSum / s.rttCount : 0,
      loss_avg: s.lossCount > 0 ? s.lossSum / s.lossCount : 0,
      bitrate_avg: s.bitrateCount > 0 ? s.bitrateSum / s.bitrateCount : 0,
    };
  }

  private async hello(conn: Connection, data: unknown) {
    if (this.sessions.ofConn(conn)) throw new TalkError(409, 'session already established');
    const d = parseData(HelloSchema, data);
    const caps = CapsSchema.safeParse(d.caps);
    const { session, resumed, previousConn } = this.sessions.hello(
      conn,
      d.client,
      caps.success ? caps.data : {},
      d.resume?.session_id,
    );
    if (previousConn?.isOpen) {
      previousConn.notify('session.kicked', { reason: 'replaced' });
      previousConn.close(CloseCode.replaced, 'replaced');
    }
    conn.log.info({ session: session.id, resumed, client: d.client }, 'session hello');

    const snapshot = resumed && session.roomId ? await this.roomView(session.roomId, session.uid) : undefined;
    if (resumed && session.roomId) this.opts.media?.onPeerOnline(session.roomId, session.uid);
    return {
      session_id: session.id,
      uid: session.uid,
      resumed,
      server_time: Date.now(),
      heartbeat_interval: this.opts.heartbeatInterval,
      resume_ttl: Math.round(this.opts.resumeTtlMs / 1000),
      ice_servers: buildIceServers(this.opts.ice, session.uid),
      ...(snapshot ? { room: snapshot } : {}),
    };
  }

  private async join(s: Session, data: unknown) {
    const d = parseData(JoinSchema, data);
    if (s.roomId) throw new TalkError(409, `already in room ${s.roomId}`);
    const videoOn = d.media?.video?.send ?? false;
    const joined = this.rooms.join({
      uid: s.uid,
      roomId: d.room_id,
      display: d.display ?? s.uid,
      platform: s.client.platform,
      muted: d.media?.audio?.muted ?? true,
      videoOn,
      ...(d.policy ? { policy: d.policy } : {}),
      ...(s.claims.rooms ? { allowedRooms: s.claims.rooms } : {}),
    });
    s.roomId = d.room_id;
    // ptt/moderated：入房即静音，等 floor 授予才 resume（发言权由服务端控制）
    if (joined.room.policy.talk_mode !== 'free' && !joined.peer.audio.muted) {
      this.rooms.setMuted(d.room_id, s.uid, true);
    }
    await this.opts.media?.addPeer(d.room_id, s.uid, joined.peer.audio.muted, videoOn);
    const view = await this.roomView(d.room_id, s.uid);
    if (!view) throw new TalkError(409, 'not in room');
    this.log.info({ uid: s.uid, room: d.room_id, media: view.media_mode }, 'room joined');
    return view;
  }

  private setPolicy(s: Session, data: unknown) {
    if (!s.roomId) throw new TalkError(409, 'not in room');
    const policy = this.rooms.setPolicy(s.roomId, s.uid, parseData(PolicyInputSchema, data));
    return { policy };
  }

  private hangup(s: Session, data: unknown) {
    const result = this.calls.hangup(s.uid, data);
    if (result.action === 'dissolve') this.rooms.dissolve(result.roomId, 'leave', 'hangup');
    else if (result.action === 'leave') this.rooms.leave(result.roomId, s.uid, 'leave');
    return {};
  }

  private leave(s: Session, data: unknown) {
    const d = parseData(LeaveSchema, data);
    if (s.roomId !== d.room_id) throw new TalkError(409, 'not in this room');
    this.rooms.leave(d.room_id, s.uid, 'leave');
    s.roomId = null;
    this.log.info({ uid: s.uid, room: d.room_id }, 'room left');
    return {};
  }

  private async setMute(s: Session, data: unknown) {
    const d = parseData(SetMuteSchema, data);
    if (!s.roomId) throw new TalkError(409, 'not in room');
    const room = this.rooms.get(s.roomId);
    if (d.source === 'mic') {
      if (!d.muted) this.checkSpeakAllowed(s, room);
      const peer = this.rooms.setMuted(s.roomId, s.uid, d.muted);
      await this.announceProducers(room, s.uid, () => this.opts.media?.setMuted(s.roomId!, s.uid, d.muted, 'mic'));
      return { audio: peer.audio };
    }
    const peer = this.rooms.setVideoSource(s.roomId, s.uid, d.source, !d.muted);
    await this.announceProducers(room, s.uid, () => this.opts.media?.setMuted(s.roomId!, s.uid, d.muted, d.source));
    return { video: peer.video };
  }

  /** 执行媒体侧状态变更并把受影响 producer 广播给其他成员（media.producer） */
  private async announceProducers(
    room: Room | undefined,
    uid: string,
    run: () =>
      Promise<{ mid: string; kind: 'audio' | 'video'; source: string; paused: boolean }[] | undefined> | undefined,
  ): Promise<void> {
    const states = (await run()) ?? [];
    if (!room) return;
    for (const st of states) this.broadcast(room, 'media.producer', { uid, ...st }, uid);
  }

  /** ptt/moderated 必须有发言权；free 模式受 max_speakers 限制 */
  private checkSpeakAllowed(s: Session, room: Room | undefined): void {
    if (!s.roomId || !room) throw new TalkError(409, 'not in room');
    const { talk_mode, max_speakers } = room.policy;
    if (talk_mode !== 'free') {
      if (this.floors.view(s.roomId).holder !== s.uid) throw new TalkError(403, 'floor required');
      return;
    }
    if (max_speakers > 0 && this.opts.media && this.opts.media.resumedAudioCount(s.roomId, s.uid) >= max_speakers) {
      throw new TalkError(429, 'max_speakers reached');
    }
  }

  private async floorRequest(s: Session) {
    if (!s.roomId) throw new TalkError(409, 'not in room');
    if (this.modes.current(s.roomId) === 'p2p') throw new TalkError(409, 'room is in p2p mode');
    return this.floors.request(s.roomId, s.uid);
  }

  private async floorRelease(s: Session) {
    if (!s.roomId) throw new TalkError(409, 'not in room');
    await this.floors.release(s.roomId, s.uid);
    return { floor: this.floors.view(s.roomId) };
  }

  private async hostGrant(s: Session, data: unknown) {
    const d = parseData(HostGrantSchema, data);
    const room = this.requireHost(s);
    // free 模式没有发言权概念，避免误用把 floor 状态写进去
    if (room.policy.talk_mode === 'free') throw new TalkError(409, 'floor is not used in free mode');
    if (!room.members.has(d.uid)) throw new TalkError(404, 'not in room');
    if (d.allow) await this.floors.grant(room.id, d.uid);
    else await this.floors.revoke(room.id, d.uid, 'host');
    return { floor: this.floors.view(room.id) };
  }

  /** 主持人全员静音：逐个静音并暂停 producer，同时清空发言权 */
  private async hostMuteAll(s: Session, data: unknown) {
    const d = parseData(HostMuteAllSchema, data);
    const room = this.requireHost(s);
    const except = new Set(d.except ?? []);
    const muted: string[] = [];
    for (const uid of [...room.members.keys()]) {
      if (except.has(uid)) continue;
      this.rooms.setMuted(room.id, uid, true);
      // 先通知本人（UI 提示），再把媒体事实广播给房间
      this.notify(uid, 'media.forceMute', { source: 'mic', by: s.uid });
      await this.announceProducers(room, uid, () => this.opts.media?.setMuted(room.id, uid, true, 'mic'));
      muted.push(uid);
    }
    this.floors.reset(room.id);
    this.broadcast(room, 'floor.state', this.floors.view(room.id));
    this.log.info({ room: room.id, by: s.uid, muted }, 'host muted all');
    return { muted };
  }

  private hostKick(s: Session, data: unknown) {
    const d = parseData(HostKickSchema, data);
    const room = this.requireHost(s);
    if (d.uid === s.uid) throw new TalkError(400, 'cannot kick yourself');
    if (!room.members.has(d.uid)) throw new TalkError(404, 'not in room');
    const target = this.sessions.ofUid(d.uid);
    this.floors.drop(room.id, d.uid);
    this.rooms.leave(room.id, d.uid, 'kicked');
    if (target) {
      target.roomId = null;
      // 先通知再断开，客户端据此提示“已被移出房间”
      target.conn?.notify('session.kicked', { reason: 'kicked' });
      target.conn?.close(CloseCode.kicked, 'kicked');
    }
    this.log.info({ room: room.id, by: s.uid, uid: d.uid }, 'peer kicked');
    return {};
  }

  private subscribeLevels(s: Session, data: unknown) {
    if (!s.roomId) throw new TalkError(409, 'not in room');
    const d = parseData(SubscribeLevelsSchema, data);
    if (d.enabled) this.levelsSubscribers.add(s.uid);
    else this.levelsSubscribers.delete(s.uid);
    return { enabled: d.enabled, media_mode: this.modes.current(s.roomId) };
  }

  private requireHost(s: Session): Room {
    const room = s.roomId ? this.rooms.get(s.roomId) : undefined;
    if (!room) throw new TalkError(409, 'not in room');
    if (room.members.get(s.uid)?.role !== 'host') throw new TalkError(403, 'host only');
    return room;
  }

  /** ActiveSpeakerObserver：仅 SFU 房间推送 */
  private onActiveSpeaker(roomId: string, uid: string): void {
    const room = this.rooms.get(roomId);
    if (!room || this.modes.current(roomId) === 'p2p') return;
    this.broadcast(room, 'room.activeSpeaker', { uid });
  }

  /** AudioLevelObserver：只发给开了 room.subscribeLevels 的成员（observer 间隔 500ms） */
  private onAudioLevels(roomId: string, entries: { uid: string; level: number }[]): void {
    const room = this.rooms.get(roomId);
    if (!room || this.levelsSubscribers.size === 0 || this.modes.current(roomId) === 'p2p') return;
    for (const uid of room.members.keys()) {
      if (!this.levelsSubscribers.has(uid)) continue;
      this.notify(uid, 'room.audioLevels', { levels: entries });
    }
  }

  private async publish(s: Session, data: unknown) {
    const d = parseData(PublishSchema, data);
    if (!s.roomId) throw new TalkError(409, 'not in room');
    if (this.modes.current(s.roomId) === 'p2p') throw new TalkError(409, 'room is in p2p mode');
    if (!this.opts.media) throw new TalkError(501, 'sfu not enabled');
    // 客户端发布时接收侧必然已就绪：重试此前未应答的 recv offer
    // （如客户端 sfu.offer handler 注册晚于他人发布，被回 501 后只能等这里恢复）
    this.opts.media.onPeerOnline(s.roomId, s.uid);
    const video = this.rooms.get(s.roomId)?.policy.video;
    const result = await this.opts.media.publish(s.roomId, s.uid, d.sdp, d.tracks, video);
    await this.announceProducers(this.rooms.get(s.roomId), s.uid, async () => result.producers);
    return result;
  }

  private async restartIce(s: Session, data: unknown) {
    const d = parseData(RestartIceSchema, data);
    if (!s.roomId || !this.opts.media) throw new TalkError(409, 'not in room');
    if (this.modes.current(s.roomId) === 'p2p') throw new TalkError(409, 'room is in p2p mode');
    await this.opts.media.restartIce(s.roomId, s.uid, d.pc);
    return {};
  }

  private broadcast(room: Room, method: string, data: unknown, exceptUid?: string): void {
    for (const uid of room.members.keys()) {
      if (uid === exceptUid) continue;
      // 断线中的成员收不到通知，恢复时由 hello 应答里的房间快照补齐
      this.sessions.ofUid(uid)?.conn?.notify(method, data);
    }
  }

  private onRoomEvent(ev: RoomEvent): void {
    switch (ev.type) {
      case 'peerJoined':
        this.broadcast(ev.room, 'room.peerJoined', ev.peer, ev.peer.uid);
        break;
      case 'peerLeft': {
        const sess = this.sessions.ofUid(ev.uid);
        if (sess?.roomId === ev.room.id) sess.roomId = null;
        this.opts.media?.removePeer(ev.room.id, ev.uid);
        this.broadcast(ev.room, 'room.peerLeft', { uid: ev.uid, reason: ev.reason });
        this.floors.drop(ev.room.id, ev.uid);
        void this.modes.sync(ev.room.id).catch((err: unknown) => this.log.error({ err }, 'mode sync failed'));
        break;
      }
      case 'peerUpdated':
        this.broadcast(ev.room, 'room.peerUpdated', { uid: ev.uid, ...ev.patch }, ev.patch.role ? undefined : ev.uid);
        break;
      case 'policyChanged':
        this.broadcast(ev.room, 'room.policyChanged', ev.policy);
        void this.modes.onPolicy(ev.room.id, ev.policy.media_mode);
        void this.floors
          .onPolicy(ev.room.id, ev.policy, ev.previous)
          .catch((err: unknown) => this.log.error({ err }, 'floor policy sync failed'));
        break;
      case 'roomClosed':
        this.opts.media?.closeRoom(ev.room.id);
        this.modes.onRoomClosed(ev.room.id);
        this.floors.reset(ev.room.id);
        this.calls.onRoomClosed(ev.room.id);
        for (const uid of ev.uids) this.notify(uid, 'room.closed', { reason: ev.reason });
        this.log.info({ room: ev.room.id, reason: ev.reason }, 'room closed');
        break;
    }
  }

  private async roomView(roomId: string, uid: string) {
    const snap = this.rooms.snapshot(roomId, uid);
    if (!snap) return undefined;
    const mode: ModeView = await this.modes.sync(roomId, uid);
    return { ...snap, floor: this.floors.view(roomId), sfu_video_codecs: [...SFU_VIDEO_CODECS], ...mode };
  }

  private notify(uid: string, method: string, data: unknown): void {
    this.sessions.ofUid(uid)?.conn?.notify(method, data);
  }

  private request(uid: string, method: string, data: unknown): Promise<unknown> {
    const conn = this.sessions.ofUid(uid)?.conn;
    if (!conn?.isOpen) return Promise.reject(new TalkError(480, 'peer offline'));
    return conn.request(method, data);
  }

  close(): void {
    this.sessions.destroyAll();
    this.calls.close();
    this.modes.close();
    this.floors.close();
  }
}
