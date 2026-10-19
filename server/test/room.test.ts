import { beforeEach, describe, expect, it } from 'vitest';
import { RoomManager, type JoinInput, type RoomEvent } from '../src/room/room-manager.js';

function input(uid: string, extra: Partial<JoinInput> = {}): JoinInput {
  return { uid, roomId: 'r-1', display: uid, platform: 'linux-arm64', muted: true, ...extra };
}

describe('RoomManager', () => {
  let events: RoomEvent[];
  let rooms: RoomManager;

  beforeEach(() => {
    events = [];
    rooms = new RoomManager({ maxPeersLimit: 20, onEvent: (e) => events.push(e) });
  });

  it('creates room on first join and makes first joiner host', () => {
    const a = rooms.join(input('a', { policy: { talk_mode: 'ptt' } }));
    expect(a.created).toBe(true);
    expect(a.peer.role).toBe('host');
    expect(a.room.policy.talk_mode).toBe('ptt');
    expect(a.room.policy.max_speakers).toBe(0);

    const b = rooms.join(input('b', { policy: { talk_mode: 'free' } }));
    expect(b.created).toBe(false);
    expect(b.peer.role).toBe('member');
    expect(b.room.policy.talk_mode).toBe('ptt');
    expect(events.map((e) => e.type)).toEqual(['peerJoined', 'peerJoined']);
  });

  it('snapshot lists other peers only', () => {
    rooms.join(input('a'));
    rooms.join(input('b'));
    const snap = rooms.snapshot('r-1', 'b');
    expect(snap?.room).toMatchObject({ room_id: 'r-1', role: 'member' });
    expect(snap?.peers.map((p) => p.uid)).toEqual(['a']);
  });

  it('rejects duplicate join, full room, forbidden room, bad id', () => {
    rooms.join(input('a', { policy: { max_peers: 2 } }));
    expect(() => rooms.join(input('a'))).toThrow(expect.objectContaining({ code: 409 }));
    rooms.join(input('b'));
    expect(() => rooms.join(input('c'))).toThrow(expect.objectContaining({ code: 429 }));
    expect(() => rooms.join(input('d', { roomId: 'r-2', allowedRooms: ['r-9'] }))).toThrow(
      expect.objectContaining({ code: 403 }),
    );
    expect(() => rooms.join(input('d', { roomId: 'bad room' }))).toThrow(expect.objectContaining({ code: 400 }));
  });

  it('clamps max_peers to server limit and rejects video', () => {
    const r = new RoomManager({ maxPeersLimit: 5, onEvent: () => {} });
    expect(r.join(input('a', { policy: { max_peers: 100 } })).room.policy.max_peers).toBe(5);
    expect(() => r.join(input('a', { roomId: 'v', policy: { video: { enabled: true } } }))).toThrow(
      expect.objectContaining({ code: 501 }),
    );
  });

  it('transfers host on host leave and closes empty room', () => {
    rooms.join(input('a'));
    rooms.join(input('b'));
    rooms.join(input('c'));
    events = [];
    rooms.leave('r-1', 'a', 'leave');
    expect(events).toMatchObject([
      { type: 'peerLeft', uid: 'a', reason: 'leave' },
      { type: 'peerUpdated', uid: 'b', patch: { role: 'host' } },
    ]);
    rooms.leave('r-1', 'b', 'timeout');
    rooms.leave('r-1', 'c', 'leave');
    expect(events.at(-1)?.type).toBe('roomClosed');
    expect(rooms.size).toBe(0);
  });

  it('setMuted emits peerUpdated only on change', () => {
    rooms.join(input('a'));
    events = [];
    rooms.setMuted('r-1', 'a', true);
    expect(events).toHaveLength(0);
    rooms.setMuted('r-1', 'a', false);
    expect(events).toMatchObject([{ type: 'peerUpdated', uid: 'a', patch: { audio: { muted: false } } }]);
    expect(() => rooms.setMuted('r-1', 'zz', false)).toThrow(expect.objectContaining({ code: 409 }));
  });
});
