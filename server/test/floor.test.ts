import { describe, expect, it } from 'vitest';
import { createLogger } from '../src/logger.js';
import { Floor, type FloorView } from '../src/room/floor.js';
import type { RoomPolicy } from '../src/room/policy.js';

const logger = createLogger({ level: 'silent', pretty: false });

function policy(patch: Partial<RoomPolicy> = {}): RoomPolicy {
  return {
    talk_mode: 'ptt',
    max_speakers: 0,
    ptt_max_hold_sec: 60,
    ptt_queue: true,
    media_mode: 'auto',
    p2p_allow_relay: true,
    max_peers: 20,
    video: { enabled: true, max_publishers: 9 },
    ...patch,
  };
}

function setup(patch: Partial<RoomPolicy> = {}, members: string[] = ['alice', 'bob', 'carol']) {
  const state = {
    policy: policy(patch),
    members: new Set(members),
    muted: new Map<string, boolean>(),
    mic: new Map<string, boolean>(),
    broadcasts: [] as { method: string; data: FloorView }[],
    notes: [] as { uid: string; method: string; data: { reason?: string } }[],
  };
  const floor = new Floor({
    logger,
    policy: () => state.policy,
    members: () => [...state.members],
    isHost: (_roomId, uid) => uid === 'alice',
    setMuted: (_roomId, uid, muted) => {
      state.muted.set(uid, muted);
    },
    applyMic: async (_roomId, uid, on) => {
      state.mic.set(uid, on);
    },
    notify: (uid, method, data) => state.notes.push({ uid, method, data: data as { reason?: string } }),
    broadcast: (_roomId, method, data) => state.broadcasts.push({ method, data: data as FloorView }),
  });
  return { floor, state, last: () => state.broadcasts.at(-1)?.data };
}

describe('floor', () => {
  it('grants an idle floor and queues the next requester', async () => {
    const { floor, state } = setup();
    expect(await floor.request('r', 'alice')).toEqual({ granted: true });
    expect(state.mic.get('alice')).toBe(true);
    expect(state.muted.get('alice')).toBe(false);
    expect(floor.view('r')).toMatchObject({ holder: 'alice', queue: [] });

    expect(await floor.request('r', 'bob')).toEqual({ granted: false, queue_pos: 1 });
    expect(floor.view('r')).toMatchObject({ holder: 'alice', queue: ['bob'] });
    // 重复申请返回同一队列位置，不重复排队
    expect(await floor.request('r', 'bob')).toEqual({ granted: false, queue_pos: 1 });
    expect(floor.view('r').queue).toEqual(['bob']);
  });

  it('hands the floor to the queue on release', async () => {
    const { floor, state, last } = setup();
    await floor.request('r', 'alice');
    await floor.request('r', 'bob');
    await floor.release('r', 'alice');
    expect(state.mic.get('alice')).toBe(false);
    expect(state.muted.get('alice')).toBe(true);
    expect(state.mic.get('bob')).toBe(true);
    expect(last()).toMatchObject({ holder: 'bob', queue: [] });
  });

  it('revokes on hold timeout and promotes the next in queue', async () => {
    const { floor, state } = setup({ ptt_max_hold_sec: 1 });
    await floor.request('r', 'alice');
    await floor.request('r', 'bob');
    await new Promise((r) => setTimeout(r, 1200));
    expect(state.notes).toEqual([{ uid: 'alice', method: 'floor.revoked', data: { reason: 'timeout' } }]);
    expect(floor.view('r')).toMatchObject({ holder: 'bob', queue: [] });
    expect(state.mic.get('alice')).toBe(false);
    expect(state.mic.get('bob')).toBe(true);
  });

  it('refuses the queue when ptt_queue is false', async () => {
    const { floor } = setup({ ptt_queue: false });
    await floor.request('r', 'alice');
    expect(await floor.request('r', 'bob')).toEqual({ granted: false });
    expect(floor.view('r')).toMatchObject({ holder: 'alice', queue: [] });
  });

  it('only speaks after host grant in moderated mode', async () => {
    const { floor, state } = setup({ talk_mode: 'moderated' });
    expect(await floor.request('r', 'bob')).toEqual({ granted: false, queue_pos: 1 });
    expect(state.mic.get('bob')).toBeUndefined();

    // 主持人自己按麦不需要自授权
    expect(await floor.request('r', 'alice')).toEqual({ granted: true });
    expect(state.mic.get('alice')).toBe(true);

    await floor.grant('r', 'bob');
    expect(floor.view('r')).toMatchObject({ holder: 'bob', queue: [] });
    expect(state.mic.get('bob')).toBe(true);

    // 主持人改授：原持有人被抢占，收到 reason=preempt
    await floor.grant('r', 'carol');
    expect(floor.view('r').holder).toBe('carol');
    expect(state.notes.at(-1)).toEqual({ uid: 'bob', method: 'floor.revoked', data: { reason: 'preempt' } });
  });

  it('drops leaving members from the queue and promotes the rest', async () => {
    const { floor } = setup();
    await floor.request('r', 'alice');
    await floor.request('r', 'bob');
    await floor.request('r', 'carol');
    expect(floor.view('r').queue).toEqual(['bob', 'carol']);

    floor.drop('r', 'bob');
    expect(floor.view('r').queue).toEqual(['carol']);

    floor.drop('r', 'alice');
    expect(floor.view('r').holder).toBe('carol');
  });

  it('pauses everyone when talk mode switches to ptt', async () => {
    const { floor, state } = setup({ talk_mode: 'free' }, ['alice', 'bob']);
    await floor.onPolicy('r', policy({ talk_mode: 'ptt' }), policy({ talk_mode: 'free' }));
    expect(state.mic.get('alice')).toBe(false);
    expect(state.mic.get('bob')).toBe(false);
    expect(state.muted.get('alice')).toBe(true);
    expect(floor.view('r')).toEqual({ holder: null, queue: [] });
  });

  it('accepts every request in free mode without holding a floor', async () => {
    const { floor, state } = setup({ talk_mode: 'free' });
    expect(await floor.request('r', 'alice')).toEqual({ granted: true });
    expect(await floor.request('r', 'bob')).toEqual({ granted: true });
    expect(state.mic.size).toBe(0);
    expect(floor.view('r')).toEqual({ holder: null, queue: [] });
  });
});
