import { z } from 'zod';

export const PolicySchema = z.object({
  talk_mode: z.enum(['free', 'ptt', 'moderated']).default('free'),
  max_speakers: z.number().int().min(0).default(0),
  ptt_max_hold_sec: z.number().int().min(1).max(3600).default(60),
  ptt_queue: z.boolean().default(true),
  media_mode: z.enum(['auto', 'sfu', 'p2p']).default('auto'),
  p2p_allow_relay: z.boolean().default(true),
  max_peers: z.number().int().min(2).default(20),
  video: z
    .object({
      enabled: z.boolean().default(false),
      max_publishers: z.number().int().min(0).default(9),
    })
    .default({ enabled: false, max_publishers: 9 }),
});

export type RoomPolicy = z.output<typeof PolicySchema>;

/** 入房请求里的 policy 是局部字段，缺省取默认值 */
export const PolicyInputSchema = PolicySchema.partial().extend({
  video: PolicySchema.shape.video.unwrap().partial().optional(),
});

export type RoomPolicyInput = z.output<typeof PolicyInputSchema>;

export function buildPolicy(input: RoomPolicyInput | undefined, maxPeersLimit: number): RoomPolicy {
  const policy = PolicySchema.parse({ ...input, video: { ...input?.video } });
  policy.max_peers = Math.min(policy.max_peers, maxPeersLimit);
  return policy;
}
