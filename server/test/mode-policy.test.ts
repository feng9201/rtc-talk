import { describe, expect, it } from 'vitest';
import { desiredMediaMode } from '../src/call/mode-policy.js';

describe('desiredMediaMode', () => {
  const base = { policy: 'auto' as const, memberCount: 2, allSupportP2p: true, stickySfu: false };

  it('uses p2p only for two peers that both support it', () => {
    expect(desiredMediaMode(base)).toBe('p2p');
    expect(desiredMediaMode({ ...base, policy: 'p2p' })).toBe('p2p');
    expect(desiredMediaMode({ ...base, memberCount: 1 })).toBe('sfu');
    expect(desiredMediaMode({ ...base, memberCount: 3 })).toBe('sfu');
    expect(desiredMediaMode({ ...base, allSupportP2p: false })).toBe('sfu');
  });

  it('stays on sfu when policy says so or the room already migrated', () => {
    expect(desiredMediaMode({ ...base, policy: 'sfu' })).toBe('sfu');
    expect(desiredMediaMode({ ...base, stickySfu: true })).toBe('sfu');
  });

  it('forces sfu for ptt and moderated rooms', () => {
    expect(desiredMediaMode({ ...base, talkMode: 'ptt' })).toBe('sfu');
    expect(desiredMediaMode({ ...base, talkMode: 'moderated' })).toBe('sfu');
    expect(desiredMediaMode({ ...base, talkMode: 'free' })).toBe('p2p');
  });
});
