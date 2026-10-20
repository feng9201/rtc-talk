import type { AddressInfo } from 'node:net';
import { createServer } from 'node:net';
import { pino } from 'pino';
import { afterEach, describe, expect, it } from 'vitest';
import { MediaNode } from '../src/media/media-node.js';

function freePort(): Promise<number> {
  return new Promise((resolve, reject) => {
    const srv = createServer();
    srv.once('error', reject);
    srv.listen(0, '127.0.0.1', () => {
      const port = (srv.address() as AddressInfo).port;
      srv.close(() => resolve(port));
    });
  });
}

describe('MediaNode concurrent room create', () => {
  let media: MediaNode | undefined;
  const lines: { msg?: string; room?: string; router?: string; uid?: string }[] = [];

  afterEach(() => {
    media?.close();
    lines.length = 0;
  });

  async function start(): Promise<MediaNode> {
    const logger = pino(
      { level: 'info' },
      {
        write(s: string) {
          lines.push(JSON.parse(s) as (typeof lines)[number]);
        },
      },
    );
    media = await MediaNode.create({
      numWorkers: 1,
      workerBin: process.env.MEDIASOUP_WORKER_BIN,
      listenIp: '127.0.0.1',
      announcedAddress: '127.0.0.1',
      port: await freePort(),
      logger,
      onWorkerDied: () => {},
    });
    media.setAnswerHandler(() => Promise.reject(new Error('no offer expected')));
    return media;
  }

  it('creates one router when many peers join a new room together', async () => {
    const node = await start();
    const room = 'race-room';
    await Promise.all(Array.from({ length: 8 }, (_, i) => node.addPeer(room, `u${i}`, false)));
    const created = lines.filter((l) => l.msg === 'router created' && l.room === room);
    expect(created).toHaveLength(1);
    for (let i = 0; i < 8; i++) await node.setMuted(room, `u${i}`, true);
    expect(lines.some((l) => l.msg === 'setMuted: peer not in media room')).toBe(false);
    node.closeRoom(room);
    const closed = lines.filter((l) => l.msg === 'router closed' && l.room === room);
    expect(closed.map((l) => l.router)).toEqual(created.map((l) => l.router));
    expect(node.status().rooms).toBe(0);
  });

  it('discards the router if the room closes while it is being created', async () => {
    const node = await start();
    const room = 'gone';
    const pending = node.addPeer(room, 'solo', true);
    node.removePeer(room, 'solo');
    node.closeRoom(room);
    await pending;
    expect(lines.filter((l) => l.msg === 'router created')).toHaveLength(0);
    expect(lines.filter((l) => l.msg === 'router discarded' && l.room === room)).toHaveLength(1);
    expect(node.status().rooms).toBe(0);
    expect(lines.some((l) => l.msg === 'setMuted: peer not in media room')).toBe(false);
  });
});
