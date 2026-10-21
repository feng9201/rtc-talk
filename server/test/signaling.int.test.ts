import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { createApp, type App } from '../src/app.js';
import { loadConfig } from '../src/config.js';
import { createLogger } from '../src/logger.js';
import { connect, type TestClient } from './helpers/ws-client.js';

const logger = createLogger({ level: 'silent', pretty: false });
const HELLO = { client: { platform: 'linux-arm64', version: '0.0.1' } };

describe('signaling over websocket', () => {
  let app: App;
  let base: string;
  const clients: TestClient[] = [];

  beforeEach(async () => {
    const config = loadConfig({
      TALK_ENV: 'dev',
      TALK_HTTP_HOST: '127.0.0.1',
      TALK_TURN_URLS: 'turn:1.2.3.4:3478?transport=udp',
      TALK_TURN_SECRET: 'secret',
      TALK_RESUME_TTL: '1',
    });
    config.http.port = 0;
    app = createApp({ config, logger });
    const addr = await app.listen();
    base = `http://127.0.0.1:${addr.port}`;
  });

  afterEach(() => {
    for (const c of clients.splice(0)) c.ws.terminate();
    app.close();
  });

  async function token(uid: string, rooms?: string): Promise<string> {
    const res = await fetch(`${base}/dev/token?uid=${uid}${rooms ? `&rooms=${rooms}` : ''}`);
    expect(res.status).toBe(200);
    return ((await res.json()) as { token: string }).token;
  }

  async function client(uid: string, hello: object = HELLO): Promise<{ c: TestClient; hello: any }> {
    const c = await connect(base, await token(uid));
    clients.push(c);
    const res = await c.req('session.hello', hello);
    expect(res.ok).toBe(true);
    return { c, hello: res.data };
  }

  it('rejects upgrade without valid token', async () => {
    await expect(connect(base, 'bad')).rejects.toThrow('http 401');
  });

  it('requires hello before other methods', async () => {
    const c = await connect(base, await token('u1'));
    clients.push(c);
    expect((await c.req('room.join', { room_id: 'r' })).error?.code).toBe(409);
    const hello = await c.req('session.hello', HELLO);
    expect(hello.data).toMatchObject({ uid: 'u1', resumed: false, heartbeat_interval: 15 });
    expect(hello.data.ice_servers[1].username).toMatch(/^\d+:u1$/);
    expect((await c.req('session.hello', HELLO)).error?.code).toBe(409);
    expect((await c.req('session.ping')).ok).toBe(true);
    expect((await c.req('nope.method')).error?.code).toBe(501);
  });

  it('two peers see each other join, mute, leave', async () => {
    const { c: a } = await client('alice');
    const { c: b } = await client('bob');

    const ja = await a.req('room.join', { room_id: 'r-1', display: 'Alice' });
    expect(ja.data).toMatchObject({ room: { room_id: 'r-1', role: 'host' }, media_mode: 'sfu', peers: [] });

    const jb = await b.req('room.join', { room_id: 'r-1', media: { audio: { muted: false } } });
    expect(jb.data.room.role).toBe('member');
    expect(jb.data.peers).toMatchObject([{ uid: 'alice', display: 'Alice', role: 'host' }]);
    expect(await a.waitNtf('room.peerJoined')).toMatchObject({ uid: 'bob', audio: { muted: false } });

    expect((await a.req('media.setMute', { source: 'mic', muted: false })).ok).toBe(true);
    expect(await b.waitNtf('room.peerUpdated')).toMatchObject({ uid: 'alice', audio: { muted: false } });
    expect((await a.req('media.setMute', { source: 'camera', muted: false })).data).toMatchObject({
      video: { on: true },
    });
    expect(await b.waitNtf('room.peerUpdated')).toMatchObject({ uid: 'alice', video: { on: true } });

    expect((await a.req('room.join', { room_id: 'r-2' })).error?.code).toBe(409);
    expect((await a.req('room.leave', { room_id: 'r-1' })).ok).toBe(true);
    expect(await b.waitNtf('room.peerLeft')).toEqual({ uid: 'alice', reason: 'leave' });
    expect(await b.waitNtf('room.peerUpdated')).toEqual({ uid: 'bob', role: 'host' });
  });

  it('resumes session after disconnect with room snapshot', async () => {
    const { c: a, hello } = await client('alice');
    const { c: b } = await client('bob');
    await a.req('room.join', { room_id: 'r-1' });
    await b.req('room.join', { room_id: 'r-1' });

    a.ws.terminate();
    await a.closed;
    const a2 = await connect(base, await token('alice'));
    clients.push(a2);
    const res = await a2.req('session.hello', { ...HELLO, resume: { session_id: hello.session_id } });
    expect(res.data).toMatchObject({ session_id: hello.session_id, resumed: true, room: { room: { room_id: 'r-1' } } });
    expect(res.data.room.peers.map((p: any) => p.uid)).toEqual(['bob']);
    expect(app.signaling.rooms.get('r-1')?.members.size).toBe(2);
  });

  it('removes peer after resume ttl expires', async () => {
    const { c: a } = await client('alice');
    const { c: b } = await client('bob');
    await a.req('room.join', { room_id: 'r-1' });
    await b.req('room.join', { room_id: 'r-1' });
    a.ws.terminate();
    expect(await b.waitNtf('room.peerLeft', undefined, 3000)).toEqual({ uid: 'alice', reason: 'timeout' });
  });

  it('new login without resume kicks old connection', async () => {
    const { c: a1 } = await client('alice');
    const { c: b } = await client('bob');
    await a1.req('room.join', { room_id: 'r-1' });
    await b.req('room.join', { room_id: 'r-1' });

    const { hello } = await client('alice');
    expect(hello.resumed).toBe(false);
    expect(await a1.waitNtf('session.kicked')).toEqual({ reason: 'replaced' });
    expect(await a1.closed).toBe(4001);
    expect(await b.waitNtf('room.peerLeft')).toEqual({ uid: 'alice', reason: 'replaced' });
  });

  it('enforces token rooms claim', async () => {
    const c = await connect(base, await token('carol', 'only-this'));
    clients.push(c);
    await c.req('session.hello', HELLO);
    expect((await c.req('room.join', { room_id: 'other' })).error?.code).toBe(403);
    expect((await c.req('room.join', { room_id: 'only-this' })).ok).toBe(true);
  });

  it('healthz reports signaling counters', async () => {
    const { c } = await client('alice');
    await c.req('room.join', { room_id: 'r-1' });
    const body = (await (await fetch(`${base}/healthz`)).json()) as any;
    expect(body.signaling).toEqual({ connections: 1, sessions: 1, rooms: 1 });
  });
});
