export type MediaMode = 'sfu' | 'p2p';

export interface ModeInput {
  policy: 'auto' | 'sfu' | 'p2p';
  memberCount: number;
  allSupportP2p: boolean;
  /** 曾经因人数或失败离开 P2P 后保持 SFU，避免 2/3 人来回切 */
  stickySfu: boolean;
  /** ptt/moderated 依赖服务端暂停 producer，只能用 SFU */
  talkMode?: 'free' | 'ptt' | 'moderated';
}

/**
 * auto / p2p：正好 2 人且都声明 caps.p2p 时走 P2P。
 * sfu、talk_mode 不是 free、人数不是 2、有人不能 P2P、或已经粘在 SFU 上时走 SFU。
 */
export function desiredMediaMode(input: ModeInput): MediaMode {
  if (input.stickySfu || input.policy === 'sfu') return 'sfu';
  if (input.talkMode && input.talkMode !== 'free') return 'sfu';
  if (input.memberCount === 2 && input.allSupportP2p) return 'p2p';
  return 'sfu';
}
