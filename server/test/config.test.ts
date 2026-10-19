import { describe, expect, it } from 'vitest';
import { loadConfig } from '../src/config.js';

describe('loadConfig', () => {
  it('uses defaults', () => {
    const c = loadConfig({});
    expect(c.env).toBe('prod');
    expect(c.http).toEqual({ host: '0.0.0.0', port: 8080 });
    expect(c.mediasoup.port).toBe(44444);
    expect(c.mediasoup.numWorkers).toBe(1);
    expect(c.turn.urls).toEqual([]);
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
    expect(c.turn.urls).toEqual(['turn:a:3478', 'turn:a:3478?transport=tcp']);
  });

  it('rejects invalid port', () => {
    expect(() => loadConfig({ TALK_HTTP_PORT: '70000' })).toThrow(/TALK_HTTP_PORT/);
  });

  it('requires TURN secret when TURN urls are set', () => {
    expect(() => loadConfig({ TALK_TURN_URLS: 'turn:a:3478' })).toThrow(/TALK_TURN_SECRET/);
  });
});
