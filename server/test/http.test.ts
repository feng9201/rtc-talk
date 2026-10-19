import type { AddressInfo } from 'node:net';
import { afterEach, describe, expect, it } from 'vitest';
import { createHttpServer, listen, type HealthInfo } from '../src/http/server.js';
import { createLogger } from '../src/logger.js';

const logger = createLogger({ level: 'silent', pretty: false });

describe('http server', () => {
  let close: (() => void) | undefined;
  afterEach(() => close?.());

  async function start(health: () => HealthInfo): Promise<string> {
    const server = createHttpServer({ logger, health });
    await listen(server, '127.0.0.1', 0);
    close = () => server.close();
    return `http://127.0.0.1:${(server.address() as AddressInfo).port}`;
  }

  it('GET /healthz returns 200 when healthy', async () => {
    const base = await start(() => ({ ok: true, rev: 'abc' }));
    const res = await fetch(`${base}/healthz`);
    expect(res.status).toBe(200);
    expect(await res.json()).toEqual({ ok: true, rev: 'abc' });
  });

  it('GET /healthz returns 503 when unhealthy', async () => {
    const base = await start(() => ({ ok: false }));
    expect((await fetch(`${base}/healthz`)).status).toBe(503);
  });

  it('unknown path returns 404', async () => {
    const base = await start(() => ({ ok: true }));
    expect((await fetch(`${base}/nope`)).status).toBe(404);
  });
});
