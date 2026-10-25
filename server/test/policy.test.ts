import { describe, expect, it } from 'vitest';
import { PolicyInputSchema, PolicySchema, buildPolicy, mergePolicy, type RoomPolicy } from '../src/room/policy.js';

const base: RoomPolicy = PolicySchema.parse({});

describe('policy input', () => {
  it('keeps unset fields absent instead of filling defaults', () => {
    const patch = PolicyInputSchema.parse({ ptt_queue: false });
    // zod 4 的 .partial() 会保留 default，这里必须真的没有键
    expect(Object.keys(patch)).toEqual(['ptt_queue']);
    expect(patch.ptt_queue).toBe(false);
  });

  it('keeps nested video fields absent too', () => {
    const patch = PolicyInputSchema.parse({ video: { max_publishers: 3 } });
    expect(Object.keys(patch.video ?? {})).toEqual(['max_publishers']);
  });

  it('still fills defaults on join via buildPolicy', () => {
    expect(buildPolicy(PolicyInputSchema.parse({ talk_mode: 'ptt' }), 20)).toEqual({
      talk_mode: 'ptt',
      max_speakers: 0,
      ptt_max_hold_sec: 60,
      ptt_queue: true,
      media_mode: 'auto',
      p2p_allow_relay: true,
      max_peers: 20,
      video: { enabled: true, max_publishers: 9 },
    });
    expect(buildPolicy(PolicyInputSchema.parse({ video: { max_publishers: 3 } }), 20).video).toEqual({
      enabled: true,
      max_publishers: 3,
    });
    expect(buildPolicy(undefined, 5).max_peers).toBe(5);
  });
});

describe('mergePolicy', () => {
  const ptt: RoomPolicy = {
    ...base,
    talk_mode: 'ptt',
    ptt_queue: false,
    media_mode: 'sfu',
    video: { enabled: false, max_publishers: 4 },
  };

  it('only touches fields given in the patch', () => {
    const next = mergePolicy(ptt, PolicyInputSchema.parse({ max_speakers: 2 }), 20);
    expect(next).toEqual({ ...ptt, max_speakers: 2 });
    expect(next.talk_mode).toBe('ptt');
    expect(next.media_mode).toBe('sfu');
    expect(next.video).toEqual({ enabled: false, max_publishers: 4 });
  });

  it('merges video field by field', () => {
    const next = mergePolicy(ptt, PolicyInputSchema.parse({ video: { max_publishers: 6 } }), 20);
    expect(next.video).toEqual({ enabled: false, max_publishers: 6 });
    expect(mergePolicy(ptt, PolicyInputSchema.parse({ video: { enabled: true } }), 20).video).toEqual({
      enabled: true,
      max_publishers: 4,
    });
  });

  it('keeps false values and clamps max_peers to the server limit', () => {
    const next = mergePolicy(base, PolicyInputSchema.parse({ ptt_queue: false, max_peers: 99 }), 8);
    expect(next.ptt_queue).toBe(false);
    expect(next.max_peers).toBe(8);
    expect(next.talk_mode).toBe('free');
  });
});
