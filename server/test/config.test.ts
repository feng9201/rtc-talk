import { describe, expect, it } from 'vitest';
import { loadConfig } from '../src/config.js';

describe('loadConfig', () => {
  const secret = 'x'.repeat(32);

  it('uses defaults', () => {
    const c = loadConfig({ TALK_JWT_SECRET: secret });
    expect(c.env).toBe('prod');
    expect(c.http).toEqual({ host: '0.0.0.0', port: 8080 });
    expect(c.mediasoup.port).toBe(44444);
    expect(c.mediasoup.numWorkers).toBe(1);
    expect(c.ice.turnUrls).toEqual([]);
    expect(c.session.idleTimeoutMs).toBe(45_000);
    expect(c.room.maxPeers).toBe(20);
  });

  it('requires JWT secret in prod', () => {
    expect(() => loadConfig({ TALK_ENV: 'prod' })).toThrow(/TALK_JWT_SECRET/);
    const c = loadConfig({ TALK_ENV: 'dev' });
    expect(c.auth.jwtSecretGenerated).toBe(true);
    expect(c.auth.jwtSecret).toHaveLength(64);
  });

  it('derives STUN urls from TURN urls', () => {
    const c = loadConfig({
      TALK_ENV: 'dev',
      TALK_TURN_URLS: 'turn:1.2.3.4:3478?transport=udp,turn:1.2.3.4:3478?transport=tcp',
      TALK_TURN_SECRET: 's',
    });
    expect(c.ice.stunUrls).toEqual(['stun:1.2.3.4:3478']);
  });

  it('parses env values', () => {
    const c = loadConfig({
      TALK_ENV: 'dev',
      TALK_HTTP_PORT: '18080',
      TALK_NUM_WORKERS: '2',
      TALK_RTC_ANNOUNCED_ADDRESS: '172.16.21.21',
      TALK_TURN_URLS: 'turn:a:3478, turn:a:3478?transport=tcp',
      TALK_TURN_SECRET: 's',
    });
    expect(c.env).toBe('dev');
    expect(c.http.port).toBe(18080);
    expect(c.mediasoup.numWorkers).toBe(2);
    expect(c.mediasoup.announcedAddress).toBe('172.16.21.21');
    expect(c.ice.turnUrls).toEqual(['turn:a:3478', 'turn:a:3478?transport=tcp']);
  });

  it('rejects invalid port', () => {
    expect(() => loadConfig({ TALK_JWT_SECRET: secret, TALK_HTTP_PORT: '70000' })).toThrow(/TALK_HTTP_PORT/);
  });

  it('requires TURN secret when TURN urls are set', () => {
    expect(() => loadConfig({ TALK_ENV: 'dev', TALK_TURN_URLS: 'turn:a:3478' })).toThrow(/TALK_TURN_SECRET/);
  });
});
