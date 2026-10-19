import { TalkError } from '../gateway/protocol.js';
import { buildPolicy, type RoomPolicy, type RoomPolicyInput } from './policy.js';

export type Role = 'host' | 'member';
export type LeaveReason = 'leave' | 'timeout' | 'kicked' | 'replaced';

export interface Peer {
  uid: string;
  display: string;
  platform: string;
  role: Role;
  audio: { muted: boolean; speaking: boolean };
  video: { on: boolean };
}

export interface Room {
  id: string;
  policy: RoomPolicy;
  /** 按入房顺序 */
  members: Map<string, Peer>;
  createdAt: number;
}

export interface JoinInput {
  uid: string;
  roomId: string;
  display: string;
  platform: string;
  muted: boolean;
  policy?: RoomPolicyInput;
  /** token 中的 rooms 声明，不给表示不限制 */
  allowedRooms?: string[];
}

/** 房间事件，由信令层投递给成员 */
export type RoomEvent =
  | { type: 'peerJoined'; room: Room; peer: Peer }
  | { type: 'peerLeft'; room: Room; uid: string; reason: LeaveReason }
  | { type: 'peerUpdated'; room: Room; uid: string; patch: Partial<Pick<Peer, 'audio' | 'video' | 'role'>> }
  | { type: 'roomClosed'; room: Room };

export const ROOM_ID_RE = /^[A-Za-z0-9_.-]{1,64}$/;

export class RoomManager {
  private readonly rooms = new Map<string, Room>();

  constructor(private readonly opts: { maxPeersLimit: number; onEvent: (ev: RoomEvent) => void }) {}

  get size(): number {
    return this.rooms.size;
  }

  get(roomId: string): Room | undefined {
    return this.rooms.get(roomId);
  }

  join(input: JoinInput): { room: Room; peer: Peer; created: boolean } {
    if (!ROOM_ID_RE.test(input.roomId)) throw new TalkError(400, 'invalid room_id');
    if (input.allowedRooms && !input.allowedRooms.includes(input.roomId)) {
      throw new TalkError(403, 'no permission to join room');
    }
    let room = this.rooms.get(input.roomId);
    const created = !room;
    if (!room) {
      const policy = buildPolicy(input.policy, this.opts.maxPeersLimit);
      if (policy.video.enabled) throw new TalkError(501, 'video is not supported yet');
      room = { id: input.roomId, policy, members: new Map(), createdAt: Date.now() };
    }
    if (room.members.has(input.uid)) throw new TalkError(409, 'already in room');
    if (room.members.size >= room.policy.max_peers) throw new TalkError(429, 'room is full');

    const peer: Peer = {
      uid: input.uid,
      display: input.display,
      platform: input.platform,
      role: created ? 'host' : 'member',
      audio: { muted: input.muted, speaking: false },
      video: { on: false },
    };
    if (created) this.rooms.set(room.id, room);
    room.members.set(peer.uid, peer);
    this.opts.onEvent({ type: 'peerJoined', room, peer });
    return { room, peer, created };
  }

  leave(roomId: string, uid: string, reason: LeaveReason): void {
    const room = this.rooms.get(roomId);
    const peer = room?.members.get(uid);
    if (!room || !peer) return;
    room.members.delete(uid);
    this.opts.onEvent({ type: 'peerLeft', room, uid, reason });

    if (room.members.size === 0) {
      this.rooms.delete(roomId);
      this.opts.onEvent({ type: 'roomClosed', room });
      return;
    }
    if (peer.role === 'host' && ![...room.members.values()].some((p) => p.role === 'host')) {
      const next = room.members.values().next().value as Peer;
      next.role = 'host';
      this.opts.onEvent({ type: 'peerUpdated', room, uid: next.uid, patch: { role: 'host' } });
    }
  }

  setMuted(roomId: string, uid: string, muted: boolean): Peer {
    const peer = this.rooms.get(roomId)?.members.get(uid);
    if (!peer) throw new TalkError(409, 'not in room');
    if (peer.audio.muted !== muted) {
      peer.audio.muted = muted;
      if (muted) peer.audio.speaking = false;
      const room = this.rooms.get(roomId) as Room;
      this.opts.onEvent({ type: 'peerUpdated', room, uid, patch: { audio: { ...peer.audio } } });
    }
    return peer;
  }

  /** room.join 应答（也用于断线恢复后的状态快照） */
  snapshot(roomId: string, uid: string) {
    const room = this.rooms.get(roomId);
    const self = room?.members.get(uid);
    if (!room || !self) return undefined;
    return {
      room: { room_id: room.id, policy: room.policy, role: self.role },
      // 媒体模式决策在 S3 实现，当前固定 SFU
      media_mode: 'sfu' as const,
      peers: [...room.members.values()].filter((p) => p.uid !== uid),
      floor: { holder: null, queue: [] as string[] },
    };
  }
}
