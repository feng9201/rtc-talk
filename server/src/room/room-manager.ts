import { TalkError } from '../gateway/protocol.js';
import { buildPolicy, type RoomPolicy, type RoomPolicyInput } from './policy.js';

export type Role = 'host' | 'member';
export type LeaveReason = 'leave' | 'timeout' | 'kicked' | 'replaced';
export type VideoSource = 'camera' | 'screen';

export interface Peer {
  uid: string;
  display: string;
  platform: string;
  role: Role;
  audio: { muted: boolean; speaking: boolean };
  /** on 是 camera || screen 的汇总值，保留给只认 {on} 的旧客端 */
  video: { camera: boolean; screen: boolean; on: boolean };
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
  /** join 声明 video.send 时摄像头初始为开 */
  videoOn?: boolean;
  policy?: RoomPolicyInput;
  /** token 中的 rooms 声明，不给表示不限制 */
  allowedRooms?: string[];
}

/** 房间事件，由信令层投递给成员 */
export type RoomEvent =
  | { type: 'peerJoined'; room: Room; peer: Peer }
  | { type: 'peerLeft'; room: Room; uid: string; reason: LeaveReason }
  | { type: 'peerUpdated'; room: Room; uid: string; patch: Partial<Pick<Peer, 'audio' | 'video' | 'role'>> }
  | { type: 'policyChanged'; room: Room; policy: RoomPolicy; previous: RoomPolicy }
  | { type: 'roomClosed'; room: Room; reason: string; uids: string[] };

export const ROOM_ID_RE = /^[A-Za-z0-9_.-]{1,64}$/;

export class RoomManager {
  private readonly rooms = new Map<string, Room>();
  /** dissolve 时带到 roomClosed，避免最后一人离开被记成普通空房 */
  private readonly closeReasons = new Map<string, { reason: string; uids: string[] }>();

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
      video: { camera: input.videoOn ?? false, screen: false, on: input.videoOn ?? false },
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
      const planned = this.closeReasons.get(roomId);
      this.closeReasons.delete(roomId);
      this.rooms.delete(roomId);
      this.opts.onEvent({
        type: 'roomClosed',
        room,
        reason: planned?.reason ?? 'empty',
        uids: planned?.uids ?? [uid],
      });
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

  /** 摄像头/屏幕共享开关（两者独立）。只在状态变化时广播 peerUpdated */
  setVideoSource(roomId: string, uid: string, source: VideoSource, on: boolean): Peer {
    const peer = this.rooms.get(roomId)?.members.get(uid);
    if (!peer) throw new TalkError(409, 'not in room');
    if (peer.video[source] !== on) {
      peer.video[source] = on;
      peer.video.on = peer.video.camera || peer.video.screen;
      const room = this.rooms.get(roomId) as Room;
      this.opts.onEvent({ type: 'peerUpdated', room, uid, patch: { video: { ...peer.video } } });
    }
    return peer;
  }

  /** 挂断 1v1：全体离开并关闭房间 */
  dissolve(roomId: string, reason: LeaveReason, closeReason: string): void {
    const room = this.rooms.get(roomId);
    if (!room) return;
    const uids = [...room.members.keys()];
    this.closeReasons.set(roomId, { reason: closeReason, uids });
    for (const uid of uids) this.leave(roomId, uid, reason);
  }

  /** 主持人修改策略。返回合并后的策略 */
  setPolicy(roomId: string, uid: string, patch: RoomPolicyInput): RoomPolicy {
    const room = this.rooms.get(roomId);
    const self = room?.members.get(uid);
    if (!room || !self) throw new TalkError(409, 'not in room');
    if (self.role !== 'host') throw new TalkError(403, 'host only');
    const policy = buildPolicy(
      { ...room.policy, ...patch, video: { ...room.policy.video, ...patch.video } },
      this.opts.maxPeersLimit,
    );
    const previous = room.policy;
    room.policy = policy;
    this.opts.onEvent({ type: 'policyChanged', room, policy, previous });
    return policy;
  }

  /** room.join 应答的房间部分（媒体模式由 ModePolicy 填） */
  snapshot(roomId: string, uid: string) {
    const room = this.rooms.get(roomId);
    const self = room?.members.get(uid);
    if (!room || !self) return undefined;
    return {
      room: { room_id: room.id, policy: room.policy, role: self.role },
      peers: [...room.members.values()].filter((p) => p.uid !== uid),
      floor: { holder: null, queue: [] as string[] },
    };
  }
}
