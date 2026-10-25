import { TalkError } from '../gateway/protocol.js';
import type { Logger } from '../logger.js';
import type { RoomPolicy } from './policy.js';

/** floor.revoked 的 reason（协议 §8） */
export type RevokeReason = 'timeout' | 'preempt' | 'host';

export interface FloorView {
  holder: string | null;
  queue: string[];
  /** 本次发言权到期时间（epoch ms）；holder 为空时不出现 */
  expires_at?: number;
}

export interface FloorDeps {
  logger: Logger;
  policy(roomId: string): RoomPolicy | undefined;
  members(roomId: string): string[];
  /** 主持人不受 moderated 排队限制（自己按麦不需要自授权） */
  isHost(roomId: string, uid: string): boolean;
  /** 同步成员静音状态（会广播 room.peerUpdated） */
  setMuted(roomId: string, uid: string, muted: boolean): void;
  /** 服务端 resume/pause 音频 producer（会广播 media.producer） */
  applyMic(roomId: string, uid: string, on: boolean): Promise<void> | undefined;
  notify(uid: string, method: string, data: unknown): void;
  broadcast(roomId: string, method: string, data: unknown): void;
}

interface FloorState {
  holder: string | null;
  queue: string[];
  expiresAt: number | undefined;
  timer: NodeJS.Timeout | undefined;
}

/**
 * 发言权（talk_mode=ptt/moderated）。
 * 同一时刻只有一个 holder；其余按申请顺序排队，超时或释放后自动递补。
 * free 模式不走这里，由各端 media.setMute 自行控制。
 */
export class Floor {
  private readonly rooms = new Map<string, FloorState>();
  private readonly log: Logger;

  constructor(private readonly deps: FloorDeps) {
    this.log = deps.logger.child({ mod: 'floor' });
  }

  view(roomId: string): FloorView {
    const st = this.rooms.get(roomId);
    if (!st) return { holder: null, queue: [] };
    if (!st.holder) return { holder: null, queue: [...st.queue] };
    const view: FloorView = { holder: st.holder, queue: [...st.queue] };
    if (st.expiresAt) view.expires_at = st.expiresAt;
    return view;
  }

  /** 申请发言：空闲即批；被占用则排队，moderated 下需主持人放行 */
  async request(roomId: string, uid: string): Promise<{ granted: boolean; queue_pos?: number }> {
    const policy = this.requirePolicy(roomId);
    if (policy.talk_mode === 'free') return { granted: true };
    const st = this.ensure(roomId);
    if (st.holder === uid) return { granted: true };
    // moderated：普通成员只能排队，等 host.grant 放行
    if (policy.talk_mode === 'moderated' && !this.deps.isHost(roomId, uid)) {
      const pos = this.enqueue(st, uid);
      this.publish(roomId);
      return { granted: false, queue_pos: pos };
    }
    if (!st.holder) {
      await this.hold(roomId, uid);
      return { granted: true };
    }
    // 不排队时直接拒绝，客户端下次按键再试
    if (policy.talk_mode === 'ptt' && !policy.ptt_queue) return { granted: false };
    const pos = this.enqueue(st, uid);
    this.publish(roomId);
    return { granted: false, queue_pos: pos };
  }

  async release(roomId: string, uid: string): Promise<void> {
    const st = this.rooms.get(roomId);
    if (!st) return;
    const queued = st.queue.includes(uid);
    st.queue = st.queue.filter((x) => x !== uid);
    if (st.holder !== uid) {
      if (queued) this.publish(roomId);
      return;
    }
    await this.dropHolder(roomId, st, uid);
    await this.next(roomId);
  }

  /** 主持人放行（host.grant allow=true）；改授他人时原持有人收到 reason=preempt */
  async grant(roomId: string, uid: string): Promise<void> {
    const st = this.ensure(roomId);
    if (st.holder === uid) return;
    const prev = st.holder;
    if (prev) await this.dropHolder(roomId, st, prev, 'preempt');
    await this.hold(roomId, uid);
  }

  /** 收回发言权（主持人 / 超时） */
  async revoke(roomId: string, uid: string, reason: RevokeReason = 'host'): Promise<void> {
    const st = this.rooms.get(roomId);
    if (!st) return;
    if (st.holder !== uid) {
      st.queue = st.queue.filter((x) => x !== uid);
      this.publish(roomId);
      return;
    }
    await this.dropHolder(roomId, st, uid, reason);
    await this.next(roomId);
  }

  /** 成员离开：清掉队列位置；持有者离开则由下一位递补 */
  drop(roomId: string, uid: string): void {
    const st = this.rooms.get(roomId);
    if (!st) return;
    st.queue = st.queue.filter((x) => x !== uid);
    if (st.holder !== uid) {
      this.publish(roomId);
      return;
    }
    st.holder = null;
    this.clearTimer(st);
    this.publish(roomId);
    void this.next(roomId);
  }

  /** 房间关闭 / 切回 free：清空发言权 */
  reset(roomId: string): void {
    const st = this.rooms.get(roomId);
    if (st) this.clearTimer(st);
    this.rooms.delete(roomId);
  }

  /** talk_mode 变化：切进 ptt/moderated 时收回发言权并暂停所有人 */
  async onPolicy(roomId: string, policy: RoomPolicy, previous: RoomPolicy): Promise<void> {
    if (previous.talk_mode === policy.talk_mode) return;
    this.reset(roomId);
    if (policy.talk_mode === 'free') {
      this.publish(roomId);
      return;
    }
    for (const uid of this.deps.members(roomId)) await this.setMic(roomId, uid, false);
    this.publish(roomId);
    this.log.info({ room: roomId, mode: policy.talk_mode }, 'floor reset for talk mode');
  }

  close(): void {
    for (const st of this.rooms.values()) this.clearTimer(st);
    this.rooms.clear();
  }

  private async hold(roomId: string, uid: string): Promise<void> {
    const st = this.ensure(roomId);
    const policy = this.requirePolicy(roomId);
    st.holder = uid;
    st.queue = st.queue.filter((x) => x !== uid);
    const holdMs = policy.ptt_max_hold_sec * 1000;
    st.expiresAt = Date.now() + holdMs;
    this.clearTimer(st);
    st.timer = setTimeout(() => void this.expire(roomId, uid), holdMs);
    st.timer.unref?.();
    await this.setMic(roomId, uid, true);
    this.publish(roomId);
  }

  /** 收回当前持有人：先暂停 producer，再清状态并广播 */
  private async dropHolder(roomId: string, st: FloorState, uid: string, reason?: RevokeReason): Promise<void> {
    st.holder = null;
    st.expiresAt = undefined;
    this.clearTimer(st);
    if (reason) this.deps.notify(uid, 'floor.revoked', { reason });
    await this.setMic(roomId, uid, false);
    this.publish(roomId);
  }

  /** 队列递补：跳过已离房的成员 */
  private async next(roomId: string): Promise<void> {
    const st = this.rooms.get(roomId);
    if (!st) return;
    const members = this.deps.members(roomId);
    while (st.queue.length > 0) {
      const uid = st.queue.shift() as string;
      if (!members.includes(uid)) continue;
      await this.hold(roomId, uid);
      return;
    }
    this.publish(roomId);
  }

  private async expire(roomId: string, uid: string): Promise<void> {
    const st = this.rooms.get(roomId);
    if (!st || st.holder !== uid) return;
    this.log.info({ room: roomId, uid }, 'floor expired');
    await this.dropHolder(roomId, st, uid, 'timeout');
    await this.next(roomId);
  }

  private async setMic(roomId: string, uid: string, on: boolean): Promise<void> {
    this.deps.setMuted(roomId, uid, !on);
    await this.deps.applyMic(roomId, uid, on);
  }

  private enqueue(st: FloorState, uid: string): number {
    const i = st.queue.indexOf(uid);
    if (i >= 0) return i + 1;
    st.queue.push(uid);
    return st.queue.length;
  }

  private publish(roomId: string): void {
    this.deps.broadcast(roomId, 'floor.state', this.view(roomId));
  }

  private clearTimer(st: FloorState): void {
    if (st.timer) {
      clearTimeout(st.timer);
      st.timer = undefined;
    }
  }

  private ensure(roomId: string): FloorState {
    let st = this.rooms.get(roomId);
    if (!st) {
      st = { holder: null, queue: [], expiresAt: undefined, timer: undefined };
      this.rooms.set(roomId, st);
    }
    return st;
  }

  private requirePolicy(roomId: string): RoomPolicy {
    const policy = this.deps.policy(roomId);
    if (!policy) throw new TalkError(409, 'not in room');
    return policy;
  }
}
