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
      enabled: z.boolean().default(true),
      max_publishers: z.number().int().min(0).default(9),
    })
    .default({ enabled: true, max_publishers: 9 }),
});

export type RoomPolicy = z.output<typeof PolicySchema>;

/** 入房 / host.setPolicy 的局部字段：全部 optional 且**不带 default**。
 * 不用 PolicySchema.partial()：zod 4 的 partial 会保留 default，缺省字段会被填成默认值，
 * 导致 host.setPolicy 只改一个字段时把整张策略表重置。 */
export const PolicyInputSchema = z.object({
  talk_mode: z.enum(['free', 'ptt', 'moderated']).optional(),
  max_speakers: z.number().int().min(0).optional(),
  ptt_max_hold_sec: z.number().int().min(1).max(3600).optional(),
  ptt_queue: z.boolean().optional(),
  media_mode: z.enum(['auto', 'sfu', 'p2p']).optional(),
  p2p_allow_relay: z.boolean().optional(),
  max_peers: z.number().int().min(2).optional(),
  video: z
    .object({
      enabled: z.boolean().optional(),
      max_publishers: z.number().int().min(0).optional(),
    })
    .optional(),
});

export type RoomPolicyInput = z.output<typeof PolicyInputSchema>;

export function buildPolicy(input: RoomPolicyInput | undefined, maxPeersLimit: number): RoomPolicy {
  const policy = PolicySchema.parse({ ...input, video: { ...input?.video } });
  policy.max_peers = Math.min(policy.max_peers, maxPeersLimit);
  return policy;
}

/** 局部更新：只为显式给出的字段赋值（undefined 不覆盖），video 逐字段合并 */
export function mergePolicy(current: RoomPolicy, patch: RoomPolicyInput, maxPeersLimit: number): RoomPolicy {
  return {
    talk_mode: patch.talk_mode ?? current.talk_mode,
    max_speakers: patch.max_speakers ?? current.max_speakers,
    ptt_max_hold_sec: patch.ptt_max_hold_sec ?? current.ptt_max_hold_sec,
    ptt_queue: patch.ptt_queue ?? current.ptt_queue,
    media_mode: patch.media_mode ?? current.media_mode,
    p2p_allow_relay: patch.p2p_allow_relay ?? current.p2p_allow_relay,
    max_peers: Math.min(patch.max_peers ?? current.max_peers, maxPeersLimit),
    video: {
      enabled: patch.video?.enabled ?? current.video.enabled,
      max_publishers: patch.video?.max_publishers ?? current.video.max_publishers,
    },
  };
}
