import { z } from 'zod';
import { buildIceServers, type IceConfig } from './auth/ice.js';
import { CallManager } from './call/call-manager.js';
import { RoomMode, type ModeView } from './call/room-mode.js';
import { CloseCode, type Connection, type ConnectionHandlers } from './gateway/connection.js';
import { parseData, TalkError, type NotifyMessage, type RequestMessage } from './gateway/protocol.js';
import type { Logger } from './logger.js';
import type { MediaNode } from './media/media-node.js';
import type { RecvOffer } from './media/room-media.js';
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
const SetMuteSchema = z.object({ source: z.enum(['mic', 'camera']), muted: z.boolean() });
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

type Handler = (session: Session, data: unknown, conn: Connection) => unknown;

const DESTROY_TO_LEAVE: Record<DestroyReason, LeaveReason> = { bye: 'leave', timeout: 'timeout', replaced: 'replaced' };

const P2P_NOTIFY = new Set(['p2p.offer', 'p2p.answer', 'p2p.candidate', 'p2p.state']);

function supportsP2p(caps: unknown): boolean {
  return typeof caps === 'object' && caps !== null && (caps as { p2p?: unknown }).p2p === true;
}

export class Signaling {
  readonly sessions: SessionManager;
  readonly rooms: RoomManager;
  private readonly calls: CallManager;
  private readonly modes: RoomMode;
  private readonly log: Logger;
  private readonly handlers: Record<string, Handler>;

  constructor(private readonly opts: SignalingOptions) {
    this.log = opts.logger.child({ mod: 'signaling' });
    this.rooms = new RoomManager({ maxPeersLimit: opts.maxPeersLimit, onEvent: (ev) => this.onRoomEvent(ev) });
    this.sessions = new SessionManager({
      resumeTtlMs: opts.resumeTtlMs,
      onDestroy: (s, reason) => {
        this.log.info({ uid: s.uid, session: s.id, reason }, 'session destroyed');
        if (s.roomId) this.rooms.leave(s.roomId, s.uid, DESTROY_TO_LEAVE[reason]);
        s.roomId = null;
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
      callerOf: (roomId) => this.calls.callerOf(roomId),
      online: (uid) => Boolean(this.sessions.ofUid(uid)?.conn?.isOpen),
      request: (uid, method, data) => this.request(uid, method, data),
      notify: (uid, method, data) => this.notify(uid, method, data),
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
      'media.setMute': (s, d) => this.setMute(s, d),
      'call.invite': (s, d) => this.calls.invite(s, d),
      'call.ringing': (s, d) => this.calls.ringing(s.uid, d),
      'call.accept': (s, d) => this.calls.accept(s.uid, d),
      'call.reject': (s, d) => this.calls.reject(s.uid, d),
      'call.cancel': (s, d) => this.calls.cancel(s.uid, d),
      'call.hangup': (s, d) => this.hangup(s, d),
      'sfu.publish': (s, d) => this.publish(s, d),
      'sfu.restartIce': (s, d) => this.restartIce(s, d),
      'sfu.stats': () => ({}),
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
    if (session?.roomId && P2P_NOTIFY.has(ntf.method)) {
      this.modes.onNotify(session.uid, session.roomId, ntf.method, ntf.data);
      return;
    }
    conn.log.debug({ method: ntf.method }, 'unhandled notify');
  }

  private async hello(conn: Connection, data: unknown) {
    if (this.sessions.ofConn(conn)) throw new TalkError(409, 'session already established');
    const d = parseData(HelloSchema, data);
    const { session, resumed, previousConn } = this.sessions.hello(conn, d.client, d.caps, d.resume?.session_id);
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
    if (d.media?.video?.send) throw new TalkError(501, 'video is not supported yet');
    const joined = this.rooms.join({
      uid: s.uid,
      roomId: d.room_id,
      display: d.display ?? s.uid,
      platform: s.client.platform,
      muted: d.media?.audio?.muted ?? true,
      ...(d.policy ? { policy: d.policy } : {}),
      ...(s.claims.rooms ? { allowedRooms: s.claims.rooms } : {}),
    });
    s.roomId = d.room_id;
    await this.opts.media?.addPeer(d.room_id, s.uid, joined.peer.audio.muted);
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
    if (d.source === 'camera') throw new TalkError(501, 'video is not supported yet');
    if (!s.roomId) throw new TalkError(409, 'not in room');
    const peer = this.rooms.setMuted(s.roomId, s.uid, d.muted);
    await this.opts.media?.setMuted(s.roomId, s.uid, d.muted);
    return { audio: peer.audio };
  }

  private async publish(s: Session, data: unknown) {
    const d = parseData(PublishSchema, data);
    if (!s.roomId) throw new TalkError(409, 'not in room');
    if (this.modes.current(s.roomId) === 'p2p') throw new TalkError(409, 'room is in p2p mode');
    if (d.tracks?.some((t) => t.kind === 'video' && !t.closed)) throw new TalkError(501, 'video is not supported yet');
    if (!this.opts.media) throw new TalkError(501, 'sfu not enabled');
    return this.opts.media.publish(s.roomId, s.uid, d.sdp);
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
        void this.modes.sync(ev.room.id).catch((err: unknown) => this.log.error({ err }, 'mode sync failed'));
        break;
      }
      case 'peerUpdated':
        this.broadcast(ev.room, 'room.peerUpdated', { uid: ev.uid, ...ev.patch }, ev.patch.role ? undefined : ev.uid);
        break;
      case 'policyChanged':
        this.broadcast(ev.room, 'room.policyChanged', ev.policy);
        void this.modes.onPolicy(ev.room.id, ev.policy.media_mode);
        break;
      case 'roomClosed':
        this.opts.media?.closeRoom(ev.room.id);
        this.modes.onRoomClosed(ev.room.id);
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
    return { ...snap, ...mode };
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
  }
}
