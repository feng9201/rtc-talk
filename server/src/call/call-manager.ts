import { randomBytes } from 'node:crypto';
import { z } from 'zod';
import { isValidUid } from '../auth/token.js';
import { parseData, TalkError } from '../gateway/protocol.js';
import type { Logger } from '../logger.js';
import { ROOM_ID_RE } from '../room/room-manager.js';

export type CallState = 'ringing' | 'accepted' | 'rejected' | 'cancelled' | 'timeout' | 'ended';

export interface CallDeps {
  logger: Logger;
  ringTimeoutMs: number;
  /** 在线且已 hello。断线保留期内视为离线 */
  lookup(uid: string): { online: boolean; roomId: string | null } | undefined;
  roomMembers(roomId: string): number;
  notify(uid: string, method: string, data: unknown): void;
}

interface Call {
  id: string;
  roomId: string;
  caller: string;
  callees: string[];
  media: unknown;
  state: CallState;
  rejected: Set<string>;
  timer: NodeJS.Timeout | undefined;
  done: boolean;
}

const InviteSchema = z.object({
  callees: z.array(z.string()).min(1).max(8),
  media: z
    .object({
      audio: z.object({ send: z.boolean().optional() }).optional(),
      video: z.object({ send: z.boolean().optional() }).optional(),
    })
    .optional(),
  room_id: z.string().optional(),
});

const CallIdSchema = z.object({ call_id: z.string().min(1).max(64) });
const RejectSchema = CallIdSchema.extend({ code: z.union([z.literal(486), z.literal(603)]) });

export interface HangupResult {
  roomId: string;
  /** dissolve：1v1 拆房；leave：只离开；none：人已经不在房里 */
  action: 'dissolve' | 'leave' | 'none';
}

export class CallManager {
  private readonly byId = new Map<string, Call>();
  private readonly byRoom = new Map<string, Call>();
  private readonly byUid = new Map<string, string>();
  private readonly log: Logger;

  constructor(private readonly deps: CallDeps) {
    this.log = deps.logger.child({ mod: 'call' });
  }

  /** 当前房间的主叫（振铃中或已接听），用来指定 P2P offerer */
  callerOf(roomId: string): string | undefined {
    const call = this.byRoom.get(roomId);
    if (!call || call.done) return undefined;
    if (call.state !== 'ringing' && call.state !== 'accepted') return undefined;
    return call.caller;
  }

  invite(caller: { uid: string; roomId: string | null }, data: unknown): { call_id: string; room_id: string } {
    const d = parseData(InviteSchema, data);
    if (d.media?.video?.send) throw new TalkError(501, 'video is not supported yet');
    if (this.byUid.has(caller.uid)) throw new TalkError(409, 'call in progress');
    const callees = [...new Set(d.callees)];
    if (callees.length !== d.callees.length) throw new TalkError(400, 'duplicate callee');
    if (callees.some((uid) => uid === caller.uid || !isValidUid(uid))) throw new TalkError(400, 'invalid callee');

    const roomId = this.pickRoom(caller, d.room_id);
    for (const uid of callees) {
      const peer = this.deps.lookup(uid);
      if (!peer?.online) throw new TalkError(480, 'callee offline');
      if (peer.roomId === roomId) throw new TalkError(409, 'callee already in room');
      if (peer.roomId || this.byUid.has(uid)) throw new TalkError(486, 'callee busy');
    }

    const call: Call = {
      id: `call-${randomBytes(8).toString('base64url')}`,
      roomId,
      caller: caller.uid,
      callees,
      media: d.media ?? {},
      state: 'ringing',
      rejected: new Set(),
      timer: undefined,
      done: false,
    };
    this.track(call);
    call.timer = setTimeout(() => this.onTimeout(call.id), this.deps.ringTimeoutMs);
    call.timer.unref?.();
    this.log.info({ call: call.id, room: roomId, caller: caller.uid, callees }, 'call invite');
    for (const uid of callees) {
      this.deps.notify(uid, 'call.incoming', {
        call_id: call.id,
        room_id: roomId,
        from: caller.uid,
        media: call.media,
      });
    }
    return { call_id: call.id, room_id: roomId };
  }

  ringing(uid: string, data: unknown): Record<string, never> {
    const call = this.require(uid, data, 'callee');
    if (call.state !== 'ringing') throw new TalkError(409, 'call is not ringing');
    this.deps.notify(call.caller, 'call.ringing', { call_id: call.id });
    this.deps.notify(call.caller, 'call.state', { call_id: call.id, state: 'ringing' });
    return {};
  }

  accept(uid: string, data: unknown): Record<string, never> {
    const call = this.require(uid, data, 'callee');
    if (call.state !== 'ringing') throw new TalkError(409, 'call is not ringing');
    call.state = 'accepted';
    this.clearTimer(call);
    this.log.info({ call: call.id, uid }, 'call accepted');
    this.tell(call, 'call.state', { call_id: call.id, state: 'accepted' }, uid);
    return {};
  }

  reject(uid: string, data: unknown): Record<string, never> {
    const d = parseData(RejectSchema, data);
    const call = this.require(uid, d, 'callee');
    if (call.state !== 'ringing') throw new TalkError(409, 'call is not ringing');
    call.rejected.add(uid);
    if (call.rejected.size < call.callees.length) {
      this.deps.notify(call.caller, 'call.state', { call_id: call.id, state: 'rejected', code: d.code });
      return {};
    }
    this.finish(call, 'rejected', d.code);
    return {};
  }

  cancel(uid: string, data: unknown): Record<string, never> {
    const call = this.require(uid, data, 'caller');
    if (call.state !== 'ringing') throw new TalkError(409, 'call is not ringing');
    this.finish(call, 'cancelled');
    return {};
  }

  hangup(uid: string, data: unknown): HangupResult {
    const call = this.require(uid, data, 'party');
    const roomId = call.roomId;
    const actor = this.deps.lookup(uid);
    const inRoom = actor?.roomId === roomId;
    const members = this.deps.roomMembers(roomId);
    if (call.state === 'ringing') {
      if (uid === call.caller) this.finish(call, 'cancelled');
      else this.finish(call, 'rejected', 603);
    } else if (call.state === 'accepted') {
      this.finish(call, 'ended');
    } else {
      throw new TalkError(409, 'call already finished');
    }
    if (!inRoom) return { roomId, action: 'none' };
    return { roomId, action: members <= 2 ? 'dissolve' : 'leave' };
  }

  onUserGone(uid: string): void {
    const call = this.byId.get(this.byUid.get(uid) ?? '');
    if (!call || call.done) return;
    if (call.state === 'ringing') {
      if (uid === call.caller) this.finish(call, 'cancelled');
      else {
        call.rejected.add(uid);
        if (call.rejected.size >= call.callees.length) this.finish(call, 'rejected', 480);
        else this.deps.notify(call.caller, 'call.state', { call_id: call.id, state: 'rejected', code: 480 });
      }
      return;
    }
    if (call.state === 'accepted') this.finish(call, 'ended');
  }

  onRoomClosed(roomId: string): void {
    const call = this.byRoom.get(roomId);
    if (!call || call.done) return;
    if (call.state === 'ringing' || call.state === 'accepted') this.finish(call, 'ended');
  }

  close(): void {
    for (const call of [...this.byId.values()]) this.clearTimer(call);
    this.byId.clear();
    this.byRoom.clear();
    this.byUid.clear();
  }

  private pickRoom(caller: { uid: string; roomId: string | null }, requested: string | undefined): string {
    if (!requested) {
      if (caller.roomId) throw new TalkError(409, 'already in room');
      let roomId = `c-${randomBytes(6).toString('base64url')}`;
      while (this.byRoom.has(roomId) || this.deps.roomMembers(roomId) > 0) {
        roomId = `c-${randomBytes(6).toString('base64url')}`;
      }
      return roomId;
    }
    if (!ROOM_ID_RE.test(requested)) throw new TalkError(400, 'invalid room_id');
    if (caller.roomId && caller.roomId !== requested) throw new TalkError(409, 'already in room');
    if (this.byRoom.has(requested)) throw new TalkError(409, 'call in progress');
    if (caller.roomId !== requested && this.deps.roomMembers(requested) > 0) throw new TalkError(409, 'room is in use');
    return requested;
  }

  private require(uid: string, data: unknown, who: 'caller' | 'callee' | 'party'): Call {
    const d = parseData(CallIdSchema, data);
    const call = this.byId.get(d.call_id);
    if (!call || call.done) throw new TalkError(404, 'call not found');
    const allowed =
      who === 'caller' ? uid === call.caller : who === 'callee' ? call.callees.includes(uid) : this.isParty(call, uid);
    if (!allowed) throw new TalkError(403, 'not part of this call');
    return call;
  }

  private isParty(call: Call, uid: string): boolean {
    return call.caller === uid || call.callees.includes(uid);
  }

  private track(call: Call): void {
    this.byId.set(call.id, call);
    this.byRoom.set(call.roomId, call);
    for (const uid of [call.caller, ...call.callees]) this.byUid.set(uid, call.id);
  }

  private onTimeout(callId: string): void {
    const call = this.byId.get(callId);
    if (!call || call.done || call.state !== 'ringing') return;
    this.log.info({ call: call.id }, 'call timeout');
    this.finish(call, 'timeout');
  }

  private finish(call: Call, state: CallState, code?: number): void {
    if (call.done) return;
    call.done = true;
    call.state = state;
    this.clearTimer(call);
    this.byId.delete(call.id);
    if (this.byRoom.get(call.roomId) === call) this.byRoom.delete(call.roomId);
    for (const uid of [call.caller, ...call.callees]) {
      if (this.byUid.get(uid) === call.id) this.byUid.delete(uid);
    }
    const data: Record<string, unknown> = { call_id: call.id, state };
    if (code !== undefined) data.code = code;
    this.tell(call, 'call.state', data);
    this.log.info({ call: call.id, state, code }, 'call finished');
  }

  private tell(call: Call, method: string, data: unknown, except?: string): void {
    for (const uid of [call.caller, ...call.callees]) {
      if (uid === except) continue;
      this.deps.notify(uid, method, data);
    }
  }

  private clearTimer(call: Call): void {
    if (call.timer) {
      clearTimeout(call.timer);
      call.timer = undefined;
    }
  }
}
