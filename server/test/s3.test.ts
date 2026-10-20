import { afterEach, describe, expect, it } from 'vitest';
import { createApp, type App } from '../src/app.js';
import { loadConfig } from '../src/config.js';
import { createLogger } from '../src/logger.js';
import { connect, type TestClient } from './helpers/ws-client.js';

const logger = createLogger({ level: 'silent', pretty: false });
const HELLO = { client: { platform: 'linux-arm64', version: '0.0.1' }, caps: { p2p: true } };

describe('call and p2p mode', () => {
  let app: App | undefined;
  const clients: TestClient[] = [];

  afterEach(() => {
    for (const c of clients.splice(0)) c.ws.terminate();
    app?.close();
    app = undefined;
  });

  async function start(extra: Record<string, string> = {}) {
    const config = loadConfig({
      TALK_ENV: 'dev',
      TALK_HTTP_HOST: '127.0.0.1',
      TALK_RATE_LIMIT: '500',
      TALK_TURN_URLS: 'turn:1.2.3.4:3478?transport=udp',
      TALK_TURN_SECRET: 'secret',
      ...extra,
    });
    config.http.port = 0;
    app = createApp({ config, logger });
    const addr = await app.listen();
    return `http://127.0.0.1:${addr.port}`;
  }

  async function open(base: string, uid: string, hello: object = HELLO) {
    const token = ((await (await fetch(`${base}/dev/token?uid=${uid}`)).json()) as { token: string }).token;
    const c = await connect(base, token);
    clients.push(c);
    const res = await c.req('session.hello', hello);
    expect(res.ok).toBe(true);
    return c;
  }

  async function join(c: TestClient, roomId: string, policy?: object) {
    return c.req('room.join', { room_id: roomId, ...(policy ? { policy } : {}) });
  }

  it('places two capable peers on p2p and forwards sdp', async () => {
    const base = await start();
    const a = await open(base, 'alice');
    const b = await open(base, 'bob');
    expect((await join(a, 'r-1')).data.media_mode).toBe('sfu');

    const offered = a.waitReq('room.mediaMode', (d) => d.mode === 'p2p');
    const jb = await join(b, 'r-1');
    const mode = await offered;
    expect(mode.p2p).toMatchObject({ role: 'offerer', peer: 'bob' });
    expect(jb.data).toMatchObject({
      media_mode: 'p2p',
      p2p: { role: 'answerer', peer: 'alice', session: mode.p2p.session },
    });

    a.ntf('p2p.offer', { session: mode.p2p.session, to: 'bob', sdp: 'v=offer' });
    expect(await b.waitNtf('p2p.offer')).toEqual({ session: mode.p2p.session, from: 'alice', sdp: 'v=offer' });
    b.ntf('p2p.answer', { session: mode.p2p.session, to: 'alice', sdp: 'v=answer' });
    expect(await a.waitNtf('p2p.answer')).toEqual({ session: mode.p2p.session, from: 'bob', sdp: 'v=answer' });
    b.ntf('p2p.candidate', {
      session: mode.p2p.session,
      to: 'alice',
      candidate: 'cand',
      sdp_mid: '0',
      sdp_mline_index: 0,
    });
    expect(await a.waitNtf('p2p.candidate')).toEqual({
      session: mode.p2p.session,
      from: 'bob',
      candidate: 'cand',
      sdp_mid: '0',
      sdp_mline_index: 0,
    });

    a.ntf('p2p.offer', { session: 'stale', to: 'bob', sdp: 'nope' });
    b.ntf('p2p.offer', { session: mode.p2p.session, to: 'alice', sdp: 'glare' });
    await expect(b.waitNtf('p2p.offer', () => true, 200)).rejects.toThrow(/timeout/);
    await expect(a.waitNtf('p2p.offer', () => true, 200)).rejects.toThrow(/timeout/);
    expect((await a.req('sfu.publish', { sdp: 'v=0' })).error).toMatchObject({ code: 409 });
  });

  it('keeps a forced sfu room on sfu', async () => {
    const base = await start();
    const a = await open(base, 'alice');
    const b = await open(base, 'bob');
    await join(a, 'r-1', { media_mode: 'sfu' });
    const unexpected = a.waitReq('room.mediaMode', (d) => d.mode === 'p2p', 300);
    const jb = await join(b, 'r-1');
    expect(jb.data.media_mode).toBe('sfu');
    await expect(unexpected).rejects.toThrow(/timeout/);
  });

  it('switches to sfu when a third peer joins and does not fall back', async () => {
    const base = await start();
    const a = await open(base, 'alice');
    const b = await open(base, 'bob');
    const c = await open(base, 'carol', { client: { platform: 'linux-arm64', version: '0.0.1' } });
    await join(a, 'r-1');
    const started = a.waitReq('room.mediaMode', (d) => d.mode === 'p2p');
    const jb = await join(b, 'r-1');
    expect(jb.data.p2p.role).toBe('answerer');
    const session = (await started).p2p.session as string;
    a.ntf('p2p.state', { session, state: 'connected', path: 'host' });
    b.ntf('p2p.state', { session, state: 'connected', path: 'host' });

    const toSfu = Promise.all([
      a.waitReq('room.mediaMode', (d) => d.mode === 'sfu'),
      b.waitReq('room.mediaMode', (d) => d.mode === 'sfu'),
    ]);
    const jc = await join(c, 'r-1');
    expect(jc.data.media_mode).toBe('sfu');
    expect(await toSfu).toMatchObject([
      { mode: 'sfu', reason: 'peer_count', media_mode: 'sfu' },
      { mode: 'sfu', reason: 'peer_count', media_mode: 'sfu' },
    ]);
    expect(await a.waitNtf('p2p.close')).toEqual({ session });
    expect(await b.waitNtf('p2p.close')).toEqual({ session });

    expect((await b.req('room.leave', { room_id: 'r-1' })).ok).toBe(true);
    const back = a.waitReq('room.mediaMode', (d) => d.mode === 'p2p', 300);
    const again = await join(b, 'r-1');
    expect(again.data.media_mode).toBe('sfu');
    await expect(back).rejects.toThrow(/timeout/);
  });

  it('falls back when p2p fails, times out, or uses a disallowed relay', async () => {
    const base = await start({ TALK_P2P_TIMEOUT: '1' });
    const a = await open(base, 'alice');
    const b = await open(base, 'bob');
    await join(a, 'fail');
    const started = a.waitReq('room.mediaMode', (d) => d.mode === 'p2p');
    await join(b, 'fail');
    const session = (await started).p2p.session as string;
    const failed = a.waitReq('room.mediaMode', (d) => d.reason === 'p2p_failed');
    a.ntf('p2p.state', { session, state: 'failed' });
    expect(await failed).toMatchObject({ mode: 'sfu', reason: 'p2p_failed' });
    expect(await b.waitReq('room.mediaMode', (d) => d.reason === 'p2p_failed')).toMatchObject({ reason: 'p2p_failed' });
    expect(await a.waitNtf('p2p.close')).toEqual({ session });
    expect((await a.req('room.leave', { room_id: 'fail' })).ok).toBe(true);
    expect((await b.req('room.leave', { room_id: 'fail' })).ok).toBe(true);

    await join(a, 'slow');
    const slow = a.waitReq('room.mediaMode', (d) => d.mode === 'p2p');
    await join(b, 'slow');
    const slowSession = (await slow).p2p.session as string;
    expect(await a.waitReq('room.mediaMode', (d) => d.reason === 'p2p_failed', 3000)).toMatchObject({
      reason: 'p2p_failed',
    });
    expect(await a.waitNtf('p2p.close')).toEqual({ session: slowSession });
    expect((await a.req('room.leave', { room_id: 'slow' })).ok).toBe(true);
    expect((await b.req('room.leave', { room_id: 'slow' })).ok).toBe(true);

    await join(a, 'relay', { p2p_allow_relay: false });
    const relay = a.waitReq('room.mediaMode', (d) => d.mode === 'p2p');
    await join(b, 'relay');
    const relaySession = (await relay).p2p.session as string;
    a.ntf('p2p.state', { session: relaySession, state: 'connected', path: 'relay' });
    expect(await a.waitReq('room.mediaMode', (d) => d.reason === 'p2p_failed')).toMatchObject({ reason: 'p2p_failed' });
    expect(await a.waitNtf('p2p.close')).toEqual({ session: relaySession });
  });

  it('drops candidates past the session limit', async () => {
    const base = await start();
    const a = await open(base, 'alice');
    const b = await open(base, 'bob');
    await join(a, 'r-1');
    const started = a.waitReq('room.mediaMode', (d) => d.mode === 'p2p');
    await join(b, 'r-1');
    const session = (await started).p2p.session as string;
    a.ntf('p2p.state', { session, state: 'connected', path: 'host' });
    const last = b.waitNtf('p2p.candidate', (d) => d.candidate === 'c-99');
    for (let i = 0; i < 100; i++) a.ntf('p2p.candidate', { session, to: 'bob', candidate: `c-${i}` });
    expect(await last).toMatchObject({ candidate: 'c-99' });
    a.ntf('p2p.candidate', { session, to: 'bob', candidate: 'c-100' });
    await expect(b.waitNtf('p2p.candidate', (d) => d.candidate === 'c-100', 300)).rejects.toThrow(/timeout/);
  });

  it('makes the caller the offerer even if they join second', async () => {
    const base = await start();
    const a = await open(base, 'alice');
    const b = await open(base, 'bob');
    const invited = await a.req('call.invite', { callees: ['bob'], room_id: 'call-room' });
    expect(invited.ok).toBe(true);
    expect(await b.waitNtf('call.incoming')).toMatchObject({
      call_id: invited.data.call_id,
      from: 'alice',
      room_id: 'call-room',
    });
    expect((await b.req('call.ringing', { call_id: invited.data.call_id })).ok).toBe(true);
    expect(await a.waitNtf('call.ringing')).toEqual({ call_id: invited.data.call_id });
    expect(await a.waitNtf('call.state')).toMatchObject({ state: 'ringing' });
    expect((await b.req('call.accept', { call_id: invited.data.call_id })).ok).toBe(true);
    expect(await a.waitNtf('call.state', (d) => d.state === 'accepted')).toMatchObject({ state: 'accepted' });

    await join(b, 'call-room');
    const toB = b.waitReq('room.mediaMode', (d) => d.mode === 'p2p');
    const ja = await join(a, 'call-room');
    expect(ja.data.p2p).toMatchObject({ role: 'offerer', peer: 'bob' });
    expect((await toB).p2p).toMatchObject({ role: 'answerer', peer: 'alice' });
  });

  it('rejects, cancels, times out and hangs up calls', async () => {
    const base = await start({ TALK_CALL_RING_SEC: '1' });
    const a = await open(base, 'alice');
    const b = await open(base, 'bob');

    expect((await a.req('call.invite', { callees: ['nobody'] })).error?.code).toBe(480);
    await join(b, 'busy');
    expect((await a.req('call.invite', { callees: ['bob'] })).error?.code).toBe(486);
    expect((await b.req('room.leave', { room_id: 'busy' })).ok).toBe(true);
    expect(await b.waitNtf('room.closed')).toEqual({ reason: 'empty' });

    const rejected = await a.req('call.invite', { callees: ['bob'], room_id: 'rej' });
    expect((await b.req('call.reject', { call_id: rejected.data.call_id, code: 603 })).ok).toBe(true);
    expect(await a.waitNtf('call.state', (d) => d.state === 'rejected')).toMatchObject({
      state: 'rejected',
      code: 603,
    });

    const cancelled = await a.req('call.invite', { callees: ['bob'], room_id: 'can' });
    expect((await a.req('call.cancel', { call_id: cancelled.data.call_id })).ok).toBe(true);
    expect(await b.waitNtf('call.state', (d) => d.call_id === cancelled.data.call_id)).toMatchObject({
      state: 'cancelled',
    });

    const timed = await a.req('call.invite', { callees: ['bob'], room_id: 'late' });
    expect(await b.waitNtf('call.state', (d) => d.state === 'timeout', 3000)).toMatchObject({
      call_id: timed.data.call_id,
      state: 'timeout',
    });

    const live = await a.req('call.invite', { callees: ['bob'], room_id: 'up' });
    expect((await b.req('call.accept', { call_id: live.data.call_id })).ok).toBe(true);
    await join(a, 'up');
    await join(b, 'up');
    expect((await a.req('call.hangup', { call_id: live.data.call_id })).ok).toBe(true);
    expect(await b.waitNtf('call.state', (d) => d.state === 'ended')).toMatchObject({ state: 'ended' });
    expect(await b.waitNtf('room.peerLeft')).toMatchObject({ uid: 'alice' });
    expect(await b.waitNtf('room.closed')).toEqual({ reason: 'hangup' });
    expect(await a.waitNtf('room.closed')).toEqual({ reason: 'hangup' });
    expect(app?.signaling.rooms.size).toBe(0);
  });

  it('lets the host force sfu', async () => {
    const base = await start();
    const a = await open(base, 'alice');
    const b = await open(base, 'bob');
    await join(a, 'r-1');
    const started = a.waitReq('room.mediaMode', (d) => d.mode === 'p2p');
    await join(b, 'r-1');
    await started;
    expect((await b.req('host.setPolicy', { media_mode: 'sfu' })).error?.code).toBe(403);
    const switched = Promise.all([
      a.waitReq('room.mediaMode', (d) => d.reason === 'policy'),
      b.waitReq('room.mediaMode', (d) => d.reason === 'policy'),
    ]);
    const res = await a.req('host.setPolicy', { media_mode: 'sfu' });
    expect(res.data.policy.media_mode).toBe('sfu');
    expect(await b.waitNtf('room.policyChanged')).toMatchObject({ media_mode: 'sfu' });
    expect(await switched).toMatchObject([
      { mode: 'sfu', reason: 'policy' },
      { mode: 'sfu', reason: 'policy' },
    ]);
    expect(await a.waitNtf('p2p.close')).toEqual(expect.objectContaining({ session: expect.any(String) }));
  });
});
