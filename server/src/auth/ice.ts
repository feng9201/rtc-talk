import { createHmac } from 'node:crypto';

export interface IceServer {
  urls: string[];
  username?: string;
  credential?: string;
  ttl?: number;
}

export interface IceConfig {
  stunUrls: string[];
  turnUrls: string[];
  turnSecret: string | undefined;
  turnTtl: number;
}

/** coturn use-auth-secret（TURN REST）临时凭证：username=<过期时间戳>:<uid>，credential=base64(HMAC-SHA1) */
export function turnCredential(secret: string, uid: string, ttlSec: number, now = Date.now()) {
  const username = `${Math.floor(now / 1000) + ttlSec}:${uid}`;
  const credential = createHmac('sha1', secret).update(username).digest('base64');
  return { username, credential };
}

export function buildIceServers(cfg: IceConfig, uid: string, now = Date.now()): IceServer[] {
  const servers: IceServer[] = [];
  if (cfg.stunUrls.length > 0) servers.push({ urls: cfg.stunUrls });
  if (cfg.turnUrls.length > 0 && cfg.turnSecret) {
    servers.push({ urls: cfg.turnUrls, ...turnCredential(cfg.turnSecret, uid, cfg.turnTtl, now), ttl: cfg.turnTtl });
  }
  return servers;
}
