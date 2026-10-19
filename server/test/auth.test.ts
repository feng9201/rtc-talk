import { createHmac } from 'node:crypto';
import { SignJWT } from 'jose';
import { describe, expect, it } from 'vitest';
import { buildIceServers, turnCredential } from '../src/auth/ice.js';
import { TokenService } from '../src/auth/token.js';

const secret = 'k'.repeat(32);

describe('TokenService', () => {
  const svc = new TokenService(secret);

  it('signs and verifies', async () => {
    const token = await svc.sign({ uid: 'u1001', rooms: ['r-1'] }, 60);
    const claims = await svc.verify(token);
    expect(claims.uid).toBe('u1001');
    expect(claims.rooms).toEqual(['r-1']);
    expect(claims.exp).toBeGreaterThan(Date.now() / 1000);
  });

  it('rejects token signed with another key', async () => {
    const token = await new TokenService('z'.repeat(32)).sign({ uid: 'u1' }, 60);
    await expect(svc.verify(token)).rejects.toMatchObject({ code: 401 });
  });

  it('rejects expired token', async () => {
    const key = new TextEncoder().encode(secret);
    const token = await new SignJWT({})
      .setProtectedHeader({ alg: 'HS256' })
      .setSubject('u1')
      .setExpirationTime(1)
      .sign(key);
    await expect(svc.verify(token)).rejects.toMatchObject({ code: 401, message: 'token expired' });
  });

  it('rejects garbage and invalid uid', async () => {
    await expect(svc.verify('abc')).rejects.toMatchObject({ code: 401 });
    await expect(svc.sign({ uid: 'bad uid' }, 60)).rejects.toMatchObject({ code: 400 });
  });
});

describe('ICE servers', () => {
  it('builds coturn REST credential', () => {
    const { username, credential } = turnCredential('s3cret', 'u1', 3600, 1_000_000_000_000);
    expect(username).toBe('1000003600:u1');
    expect(credential).toBe(createHmac('sha1', 's3cret').update(username).digest('base64'));
  });

  it('includes STUN and TURN entries', () => {
    const servers = buildIceServers(
      { stunUrls: ['stun:h:3478'], turnUrls: ['turn:h:3478'], turnSecret: 's', turnTtl: 60 },
      'u1',
    );
    expect(servers).toHaveLength(2);
    expect(servers[0]).toEqual({ urls: ['stun:h:3478'] });
    expect(servers[1]).toMatchObject({ urls: ['turn:h:3478'], ttl: 60 });
    expect(servers[1]?.username).toMatch(/:u1$/);
  });

  it('omits TURN without secret', () => {
    expect(
      buildIceServers({ stunUrls: [], turnUrls: ['turn:h:3478'], turnSecret: undefined, turnTtl: 60 }, 'u1'),
    ).toEqual([]);
  });
});
