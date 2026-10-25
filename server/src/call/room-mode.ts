import { randomBytes } from 'node:crypto';
import { z } from 'zod';
import type { Logger } from '../logger.js';
import type { Room } from '../room/room-manager.js';
import { desiredMediaMode, type MediaMode } from './mode-policy.js';

const MAX_CANDIDATES = 100;

export type ModeReason = 'peer_count' | 'p2p_failed' | 'policy';

export interface P2pAssignment {
  peer: string;
  role: 'offerer' | 'answerer';
  session: string;
}

export interface ModeView {
  media_mode: MediaMode;
  p2p?: P2pAssignment;
  /** P2P 链路能否承载视频（双方 caps.p2p_video） */
  p2p_video?: boolean;
}

export interface RoomModeDeps {
  logger: Logger;
  p2pTimeoutMs: number;
  room(roomId: string): Room | undefined;
  supportsP2p(uid: string): boolean;
  supportsP2pVideo(uid: string): boolean;
  callerOf(roomId: string): string | undefined;
  online(uid: string): boolean;
  request(uid: string, method: string, data: unknown): Promise<unknown>;
  notify(uid: string, method: string, data: unknown): void;
}

interface P2pSession {
  id: string;
  offerer: string;
  answerer: string;
  candidates: number;
  connected: boolean;
  path?: 'host' | 'srflx' | 'relay';
  timer?: NodeJS.Timeout;
}

interface RoomState {
  mode: MediaMode;
  stickySfu: boolean;
  pendingReason?: ModeReason;
  p2p?: P2pSession;
  closed: boolean;
  chain: Promise<void>;
  /** 正在用自己的应答带走模式结果的 uid，不再另发 room.mediaMode */
  silent: Set<string>;
}

const OfferSchema = z.object({
  session: z.string().min(1).max(64),
  to: z.string().min(1).max(64),
  sdp: z.string().min(1).max(65_536),
});

const CandidateSchema = z.object({
  session: z.string().min(1).max(64),
  to: z.string().min(1).max(64),
  candidate: z.string().max(2000),
  sdp_mid: z.string().max(32).optional(),
  sdp_mline_index: z.number().int().nonnegative().optional(),
});

const StateSchema = z.object({
  session: z.string().min(1).max(64),
  state: z.enum(['connecting', 'connected', 'failed', 'closed']),
  path: z.enum(['host', 'srflx', 'relay']).optional(),
});

export class RoomMode {
  private readonly rooms = new Map<string, RoomState>();
  private readonly log: Logger;

  constructor(private readonly deps: RoomModeDeps) {
    this.log = deps.logger.child({ mod: 'mode' });
  }

  current(roomId: string): MediaMode {
    return this.rooms.get(roomId)?.mode ?? 'sfu';
  }

  /**
   * 按当前房间重新决策。uid 非空时，该成员的结果由返回值带走（入房应答 / hello 快照），
   * 不重复发 room.mediaMode。
   */
  async sync(roomId: string, uid = ''): Promise<ModeView> {
    const state = this.ensure(roomId);
    let view: ModeView = { media_mode: 'sfu' };
    await this.enqueue(state, () => {
      if (state.closed) return;
      if (uid) state.silent.add(uid);
      try {
        this.step(roomId, state);
        if (uid) view = this.view(state, uid);
      } finally {
        if (uid) state.silent.delete(uid);
      }
    });
    return view;
  }

  /** 主持人改了 media_mode。p2p 明确要求时允许从粘滞 SFU 再试一次 */
  async onPolicy(roomId: string, mediaMode: 'auto' | 'sfu' | 'p2p'): Promise<void> {
    const state = this.rooms.get(roomId);
    if (!state || state.closed) return;
    await this.enqueue(state, () => {
      if (state.closed) return;
      if (mediaMode === 'sfu') state.stickySfu = true;
      if (mediaMode === 'p2p') state.stickySfu = false;
      this.step(roomId, state);
    }).catch((err: unknown) => this.log.error({ err, room: roomId }, 'policy mode sync failed'));
  }

  onNotify(uid: string, roomId: string, method: string, data: unknown): void {
    const state = this.rooms.get(roomId);
    if (!state || state.closed) return;
    void this.enqueue(state, () => {
      if (state.closed) return;
      if (method === 'p2p.state') this.applyState(roomId, state, uid, data);
      else this.forward(state, uid, method, data);
    }).catch(() => undefined);
  }

  onRoomClosed(roomId: string): void {
    const state = this.rooms.get(roomId);
    if (!state) return;
    state.closed = true;
    this.dropSession(state);
    void state.chain.then(() => {
      if (this.rooms.get(roomId) === state) this.rooms.delete(roomId);
    });
  }

  close(): void {
    for (const state of this.rooms.values()) {
      state.closed = true;
      this.dropSession(state);
    }
    this.rooms.clear();
  }

  private ensure(roomId: string): RoomState {
    let state = this.rooms.get(roomId);
    if (!state) {
      state = { mode: 'sfu', stickySfu: false, closed: false, chain: Promise.resolve(), silent: new Set() };
      this.rooms.set(roomId, state);
    }
    return state;
  }

  private enqueue(state: RoomState, fn: () => void): Promise<void> {
    const run = state.chain.then(fn, fn);
    state.chain = run.then(
      () => undefined,
      (err: unknown) => {
        this.log.error({ err }, 'room mode failed');
      },
    );
    return run;
  }

  private step(roomId: string, state: RoomState): void {
    const room = this.deps.room(roomId);
    if (!room || state.closed) return;
    const members = [...room.members.keys()];
    if (state.mode === 'p2p' && state.p2p?.path === 'relay' && !room.policy.p2p_allow_relay) {
      state.stickySfu = true;
      state.pendingReason = 'p2p_failed';
    }
    const desired = desiredMediaMode({
      policy: room.policy.media_mode,
      memberCount: members.length,
      allSupportP2p: members.length > 0 && members.every((uid) => this.deps.supportsP2p(uid)),
      stickySfu: state.stickySfu,
      talkMode: room.policy.talk_mode,
    });
    if (desired === state.mode) {
      state.pendingReason = undefined;
      return;
    }
    if (desired === 'p2p') this.enterP2p(roomId, state, members);
    else this.leaveP2p(roomId, state, room, members.length);
  }

  private enterP2p(roomId: string, state: RoomState, members: string[]): void {
    const caller = this.deps.callerOf(roomId);
    const offerer = caller && members.includes(caller) ? caller : members[0]!;
    const answerer = members.find((uid) => uid !== offerer)!;
    const session: P2pSession = {
      id: `p2p-${randomBytes(6).toString('base64url')}`,
      offerer,
      answerer,
      candidates: 0,
      connected: false,
    };
    session.timer = setTimeout(() => {
      void this.onTimeout(roomId, session.id);
    }, this.deps.p2pTimeoutMs);
    session.timer.unref?.();
    state.p2p = session;
    state.mode = 'p2p';
    state.pendingReason = undefined;
    this.log.info({ room: roomId, session: session.id, offerer, answerer }, 'p2p started');
    for (const uid of members) {
      if (state.silent.has(uid)) continue;
      const payload = this.p2pPayload(session, uid, 'peer_count');
      void this.deps.request(uid, 'room.mediaMode', payload).catch((err: unknown) => {
        this.log.warn({ err, uid, room: roomId }, 'p2p mediaMode failed');
      });
    }
  }

  private leaveP2p(roomId: string, state: RoomState, room: Room, memberCount: number): void {
    const reason: ModeReason = state.pendingReason ?? (memberCount > 2 ? 'peer_count' : 'policy');
    state.pendingReason = undefined;
    const stick = reason === 'p2p_failed' || reason === 'policy' || (reason === 'peer_count' && memberCount > 2);
    if (stick) state.stickySfu = true;
    const session = state.p2p;
    state.mode = 'sfu';
    if (!session) return;
    this.stopTimer(session);
    if (memberCount < 2) {
      this.emitClose(session);
      state.p2p = undefined;
      this.log.info({ room: roomId, session: session.id, reason }, 'p2p closed');
      return;
    }
    const targets = [session.offerer, session.answerer].filter((uid) => room.members.has(uid));
    this.log.info({ room: roomId, session: session.id, reason, targets }, 'switching to sfu');
    void this.finishSwitch(roomId, state, session.id, targets, reason).catch((err: unknown) => {
      this.log.error({ err, room: roomId }, 'sfu switch failed');
    });
  }

  private async finishSwitch(
    roomId: string,
    state: RoomState,
    sessionId: string,
    targets: string[],
    reason: ModeReason,
  ): Promise<void> {
    const payload = { mode: 'sfu' as const, reason, media_mode: 'sfu' as const };
    const acks = await Promise.all(
      targets.map(async (uid) => {
        if (!this.deps.online(uid)) return true;
        try {
          await this.deps.request(uid, 'room.mediaMode', payload);
          return true;
        } catch (err) {
          this.log.warn({ err, uid, room: roomId, session: sessionId }, 'sfu mediaMode failed');
          return false;
        }
      }),
    );
    if (state.closed || state.p2p?.id !== sessionId) return;
    if (acks.some((ok) => !ok)) return;
    for (const uid of targets) {
      if (this.deps.online(uid)) this.deps.notify(uid, 'p2p.close', { session: sessionId });
    }
    if (state.p2p?.id === sessionId) state.p2p = undefined;
    this.log.info({ room: roomId, session: sessionId }, 'p2p closed after sfu ready');
  }

  private onTimeout(roomId: string, sessionId: string): Promise<void> {
    const state = this.rooms.get(roomId);
    if (!state) return Promise.resolve();
    return this.enqueue(state, () => {
      if (state.closed || state.p2p?.id !== sessionId || state.p2p.connected || state.mode !== 'p2p') return;
      state.stickySfu = true;
      state.pendingReason = 'p2p_failed';
      this.log.info({ room: roomId, session: sessionId }, 'p2p timeout');
      this.step(roomId, state);
    }).catch(() => undefined);
  }

  private applyState(roomId: string, state: RoomState, uid: string, data: unknown): void {
    const parsed = StateSchema.safeParse(data);
    if (!parsed.success) return;
    const session = state.p2p;
    if (!session || session.id !== parsed.data.session || state.mode !== 'p2p') return;
    if (uid !== session.offerer && uid !== session.answerer) return;
    const room = this.deps.room(roomId);
    if (!room) return;
    if (parsed.data.path) session.path = parsed.data.path;
    if (parsed.data.state === 'connected') {
      session.connected = true;
      this.stopTimer(session);
      this.log.info({ room: roomId, session: session.id, path: session.path }, 'p2p connected');
    }
    const relayBlocked = session.path === 'relay' && !room.policy.p2p_allow_relay;
    if (parsed.data.state === 'failed' || relayBlocked) {
      state.stickySfu = true;
      state.pendingReason = 'p2p_failed';
      this.step(roomId, state);
    }
  }

  private forward(state: RoomState, uid: string, method: string, data: unknown): void {
    if (method === 'p2p.candidate') {
      const parsed = CandidateSchema.safeParse(data);
      if (!parsed.success) {
        this.log.debug({ method, uid }, 'drop bad p2p message');
        return;
      }
      const routed = this.route(state, uid, parsed.data.session, parsed.data.to, 'any');
      if (!routed) return;
      routed.session.candidates += 1;
      if (routed.session.candidates > MAX_CANDIDATES) {
        if (routed.session.candidates === MAX_CANDIDATES + 1) {
          this.log.warn({ session: routed.session.id }, 'p2p candidate limit');
        }
        return;
      }
      const body: Record<string, unknown> = {
        session: routed.session.id,
        from: uid,
        candidate: parsed.data.candidate,
      };
      if (parsed.data.sdp_mid !== undefined) body.sdp_mid = parsed.data.sdp_mid;
      if (parsed.data.sdp_mline_index !== undefined) body.sdp_mline_index = parsed.data.sdp_mline_index;
      this.deps.notify(routed.peer, method, body);
      return;
    }
    const parsed = OfferSchema.safeParse(data);
    if (!parsed.success) {
      this.log.debug({ method, uid }, 'drop bad p2p message');
      return;
    }
    const role = method === 'p2p.offer' ? 'offerer' : 'answerer';
    const routed = this.route(state, uid, parsed.data.session, parsed.data.to, role);
    if (!routed) return;
    this.deps.notify(routed.peer, method, { session: routed.session.id, from: uid, sdp: parsed.data.sdp });
  }

  private route(
    state: RoomState,
    uid: string,
    sessionId: string,
    to: string,
    role: 'offerer' | 'answerer' | 'any',
  ): { session: P2pSession; peer: string } | undefined {
    const session = state.p2p;
    if (!session || session.id !== sessionId) return undefined;
    const from = uid === session.offerer ? 'offerer' : uid === session.answerer ? 'answerer' : undefined;
    if (!from || (role !== 'any' && from !== role)) return undefined;
    const peer = from === 'offerer' ? session.answerer : session.offerer;
    if (to !== peer) return undefined;
    return { session, peer };
  }

  private view(state: RoomState, uid: string): ModeView {
    const session = state.p2p;
    if (state.mode !== 'p2p' || !session) return { media_mode: 'sfu' };
    if (uid !== session.offerer && uid !== session.answerer) return { media_mode: 'sfu' };
    const role = uid === session.offerer ? 'offerer' : 'answerer';
    return {
      media_mode: 'p2p',
      p2p: { peer: role === 'offerer' ? session.answerer : session.offerer, role, session: session.id },
      p2p_video: this.p2pVideo(session),
    };
  }

  private p2pPayload(session: P2pSession, uid: string, reason: ModeReason) {
    const role = uid === session.offerer ? 'offerer' : 'answerer';
    return {
      mode: 'p2p' as const,
      reason,
      media_mode: 'p2p' as const,
      p2p_video: this.p2pVideo(session),
      p2p: {
        peer: role === 'offerer' ? session.answerer : session.offerer,
        role,
        session: session.id,
      },
    };
  }

  /** 双方都声明 caps.p2p_video 才允许在 P2P 链路上加视频 m-line */
  private p2pVideo(session: P2pSession): boolean {
    return this.deps.supportsP2pVideo(session.offerer) && this.deps.supportsP2pVideo(session.answerer);
  }

  private emitClose(session: P2pSession): void {
    this.stopTimer(session);
    for (const uid of [session.offerer, session.answerer]) {
      if (this.deps.online(uid)) this.deps.notify(uid, 'p2p.close', { session: session.id });
    }
  }

  private dropSession(state: RoomState): void {
    if (!state.p2p) return;
    this.stopTimer(state.p2p);
    state.p2p = undefined;
  }

  private stopTimer(session: P2pSession): void {
    if (session.timer) {
      clearTimeout(session.timer);
      session.timer = undefined;
    }
  }
}
