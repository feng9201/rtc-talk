#!/usr/bin/env -S npx tsx
import { parseArgs } from 'node:util';
import { AudioSender } from './audio.js';
import { SignalClient } from './signal-client.js';

const HELP = `用法: talk-bot --url http://HOST:PORT [选项]

  --url        服务地址（http/https），token 从 <url>/dev/token 获取，信令连 <url>/talk/v1/ws
  --room       房间号（默认 bot-room）
  --count      机器人数量（默认 2）
  --duration   全员入房后停留秒数（默认 5），期间按心跳间隔 ping、轮流切换静音
  --prefix     uid 前缀（默认 bot）
  --resume     停留期间让第一个机器人断线后用 session_id 恢复，校验房间快照
  --audio      开麦并发布 Opus，停留期间互发 RTP，校验收包数（此时不做静音轮换）
  -h, --help
`;

interface Peer {
  uid: string;
  audio?: { muted: boolean };
}

interface Bot {
  uid: string;
  client: SignalClient;
  sessionId: string;
  heartbeat: number;
  /** 当前看到的其他成员 */
  others: Map<string, Peer>;
  events: string[];
  audio?: AudioSender;
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

function log(msg: string): void {
  console.log(`${new Date().toISOString().slice(11, 23)} ${msg}`);
}

async function fetchToken(base: string, uid: string, room: string): Promise<string> {
  const url = new URL('/dev/token', base);
  url.searchParams.set('uid', uid);
  url.searchParams.set('rooms', room);
  const res = await fetch(url);
  if (!res.ok) throw new Error(`GET /dev/token -> HTTP ${res.status}: ${await res.text()}`);
  return ((await res.json()) as { token: string }).token;
}

function wsUrl(base: string): string {
  const u = new URL('/talk/v1/ws', base);
  u.protocol = u.protocol === 'https:' ? 'wss:' : 'ws:';
  return u.toString();
}

function track(bot: Bot): void {
  bot.client.onNotify((method, data) => {
    const uid = String(data.uid ?? '');
    bot.events.push(`${method}:${uid}`);
    if (method === 'room.peerJoined') bot.others.set(uid, data as unknown as Peer);
    else if (method === 'room.peerLeft') bot.others.delete(uid);
    else if (method === 'room.peerUpdated') {
      const p = bot.others.get(uid);
      if (p && data.audio) p.audio = data.audio as Peer['audio'];
    } else if (method === 'session.kicked') log(`${bot.uid} kicked: ${JSON.stringify(data)}`);
  });
}

const HELLO = { client: { platform: `bot-${process.platform}-${process.arch}`, version: '0.1.0', sdk: 'talk-bot' } };

async function startBot(base: string, room: string, uid: string): Promise<Bot> {
  const t0 = performance.now();
  const token = await fetchToken(base, uid, room);
  const client = await SignalClient.connect(wsUrl(base), token);
  const hello = await client.request<{ session_id: string; heartbeat_interval: number; ice_servers: unknown[] }>(
    'session.hello',
    HELLO,
  );
  const bot: Bot = {
    uid,
    client,
    sessionId: hello.session_id,
    heartbeat: hello.heartbeat_interval,
    others: new Map(),
    events: [],
  };
  track(bot);
  const join = await client.request<{ room: { role: string }; media_mode: string; peers: Peer[] }>('room.join', {
    room_id: room,
    display: uid,
    media: { audio: { send: true, muted: true } },
  });
  for (const p of join.peers) bot.others.set(p.uid, p);
  log(
    `${uid} joined as ${join.room.role}, mode=${join.media_mode}, peers=${join.peers.length}, ` +
      `ice_servers=${hello.ice_servers.length}, ${Math.round(performance.now() - t0)}ms`,
  );
  return bot;
}

async function resumeBot(base: string, room: string, bot: Bot): Promise<string[]> {
  bot.client.terminate();
  await bot.client.closed;
  await sleep(300);
  const token = await fetchToken(base, bot.uid, room);
  const client = await SignalClient.connect(wsUrl(base), token);
  const hello = await client.request<{ resumed: boolean; room?: { peers: Peer[] } }>('session.hello', {
    ...HELLO,
    resume: { session_id: bot.sessionId },
  });
  if (!hello.resumed || !hello.room) return [`${bot.uid}: resume failed (resumed=${hello.resumed})`];
  bot.client = client;
  bot.others = new Map(hello.room.peers.map((p) => [p.uid, p]));
  track(bot);
  log(`${bot.uid} resumed, snapshot peers=${hello.room.peers.length}`);
  return [];
}

function checkVisibility(bots: Bot[], stage: string): string[] {
  const errors: string[] = [];
  for (const b of bots) {
    const expected = bots.filter((o) => o !== b).map((o) => o.uid);
    const seen = [...b.others.keys()];
    const missing = expected.filter((u) => !b.others.has(u));
    const extra = seen.filter((u) => !expected.includes(u));
    if (missing.length || extra.length) {
      errors.push(`[${stage}] ${b.uid}: missing=${missing.join(',') || '-'} extra=${extra.join(',') || '-'}`);
    }
  }
  return errors;
}

async function main(): Promise<number> {
  const { values } = parseArgs({
    options: {
      url: { type: 'string' },
      room: { type: 'string', default: 'bot-room' },
      count: { type: 'string', default: '2' },
      duration: { type: 'string', default: '5' },
      prefix: { type: 'string', default: 'bot' },
      resume: { type: 'boolean', default: false },
      audio: { type: 'boolean', default: false },
      help: { type: 'boolean', short: 'h', default: false },
    },
  });
  if (values.help || !values.url) {
    process.stdout.write(HELP);
    return values.help ? 0 : 2;
  }
  const base = values.url;
  const room = values.room;
  const count = Number(values.count);
  const duration = Number(values.duration);
  if (!(count >= 1) || !(duration >= 0)) throw new Error('--count/--duration 无效');

  const errors: string[] = [];
  const bots: Bot[] = [];
  for (let i = 1; i <= count; i++) bots.push(await startBot(base, room, `${values.prefix}-${i}`));

  await sleep(300);
  errors.push(...checkVisibility(bots, 'after-join'));

  let resumed = !values.resume;
  if (values.audio && values.resume) {
    errors.push(...(await resumeBot(base, room, bots[0]!)));
    errors.push(...checkVisibility(bots, 'after-resume'));
    resumed = true;
  }

  if (values.audio) {
    const loopback = new URL(base).hostname === '127.0.0.1' || new URL(base).hostname === 'localhost';
    for (const b of bots) {
      await b.client.request('media.setMute', { source: 'mic', muted: false });
      b.audio = new AudioSender(b.client, loopback);
      await b.audio.publish();
    }
    await Promise.race([
      Promise.all(bots.map((b) => b.audio!.subscribed)),
      sleep(8000).then(() => {
        throw new Error('等待 sfu.offer 超时');
      }),
    ]);
    for (const b of bots) b.audio!.start();
    await sleep(Math.max(duration, 2) * 1000);
    for (const b of bots) {
      const expectFrom = bots.length - 1;
      if (expectFrom > 0 && b.audio!.received < expectFrom * 10) {
        errors.push(`[audio] ${b.uid} received ${b.audio!.received} rtp packets, expected from ${expectFrom} peers`);
      } else if (expectFrom > 0) {
        log(`${b.uid} received ${b.audio!.received} rtp packets`);
      }
      b.audio!.stop();
    }
  }

  const end = Date.now() + (values.audio ? 0 : duration) * 1000;
  const interval = Math.max(1, Math.min(...bots.map((b) => b.heartbeat))) * 1000;
  const muted = new Map(bots.map((b) => [b.uid, true]));
  let round = 0;
  while (Date.now() < end) {
    const rtts = await Promise.all(
      bots.map(async (b) => {
        const t = performance.now();
        await b.client.request('session.ping');
        return performance.now() - t;
      }),
    );
    const speaker = bots[round % bots.length]!;
    const next = !muted.get(speaker.uid);
    await speaker.client.request('media.setMute', { source: 'mic', muted: next });
    muted.set(speaker.uid, next);
    log(
      `ping rtt avg=${(rtts.reduce((a, b) => a + b, 0) / rtts.length).toFixed(1)}ms, ` +
        `${speaker.uid} ${next ? 'muted' : 'unmuted'}`,
    );
    round++;
    if (!resumed) {
      resumed = true;
      errors.push(...(await resumeBot(base, room, bots[0]!)));
      errors.push(...checkVisibility(bots, 'after-resume'));
    }
    await sleep(Math.min(interval, Math.max(0, end - Date.now())));
  }

  await sleep(200);
  errors.push(...checkVisibility(bots, 'before-leave'));
  if (!values.audio) {
    for (const b of bots) {
      for (const [uid, p] of b.others) {
        if (p.audio?.muted !== muted.get(uid)) {
          errors.push(`[mute] ${b.uid} sees ${uid} muted=${p.audio?.muted}, expected ${muted.get(uid)}`);
        }
      }
    }
  }

  for (const [i, b] of bots.entries()) {
    await b.client.request('room.leave', { room_id: room });
    await sleep(100);
    const rest = bots.slice(i + 1);
    for (const r of rest) {
      if (r.others.has(b.uid)) errors.push(`[after-leave] ${r.uid} still sees ${b.uid}`);
    }
    await b.client.request('session.bye');
    await b.client.closed;
  }
  log(`${count} bots left room ${room}`);

  if (errors.length) {
    for (const e of errors) console.error(`FAIL ${e}`);
    return 1;
  }
  log('OK: all bots saw each other join, update and leave');
  return 0;
}

main().then(
  (code) => process.exit(code),
  (err: unknown) => {
    console.error(`FAIL ${(err as Error).message}`);
    process.exit(1);
  },
);
