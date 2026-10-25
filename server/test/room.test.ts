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

  it('clamps max_peers to server limit and allows single-layer video policy', () => {
    const r = new RoomManager({ maxPeersLimit: 5, onEvent: () => {} });
    expect(r.join(input('a', { policy: { max_peers: 100 } })).room.policy.max_peers).toBe(5);
    const video = r.join(input('a', { roomId: 'v', videoOn: true, policy: { video: { enabled: true } } }));
    expect(video.room.policy.video.enabled).toBe(true);
    expect(video.peer.video.on).toBe(true);
    // 默认开启视频：现有客户端 join 不带 video policy，房间需可直接发布视频
    expect(r.join(input('b', { roomId: 'v3' })).room.policy.video).toEqual({ enabled: true, max_publishers: 9 });
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

  it('setVideoSource tracks camera and screen independently', () => {
    rooms.join(input('a'));
    events = [];
    rooms.setVideoSource('r-1', 'a', 'camera', false);
    expect(events).toHaveLength(0);
    rooms.setVideoSource('r-1', 'a', 'camera', true);
    expect(events).toMatchObject([
      { type: 'peerUpdated', uid: 'a', patch: { video: { camera: true, screen: false, on: true } } },
    ]);
    events = [];
    rooms.setVideoSource('r-1', 'a', 'screen', true);
    expect(events).toMatchObject([{ type: 'peerUpdated', uid: 'a', patch: { video: { screen: true, on: true } } }]);
    // 摄像头关掉后屏幕还开着，汇总值仍为 true
    rooms.setVideoSource('r-1', 'a', 'camera', false);
    expect(rooms.get('r-1')?.members.get('a')?.video).toEqual({ camera: false, screen: true, on: true });
    rooms.setVideoSource('r-1', 'a', 'screen', false);
    expect(rooms.get('r-1')?.members.get('a')?.video).toEqual({ camera: false, screen: false, on: false });
    expect(() => rooms.setVideoSource('r-1', 'zz', 'screen', true)).toThrow(expect.objectContaining({ code: 409 }));
  });
});
