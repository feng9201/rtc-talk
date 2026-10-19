import { errors, jwtVerify, SignJWT } from 'jose';
import { TalkError } from '../gateway/protocol.js';

export interface TokenClaims {
  uid: string;
  /** 允许进入的房间；不给表示不限制 */
  rooms?: string[];
  roles?: string[];
  exp: number;
}

const ALG = 'HS256';
const UID_RE = /^[A-Za-z0-9_.@-]{1,64}$/;

export function isValidUid(uid: string): boolean {
  return UID_RE.test(uid);
}

export class TokenService {
  private readonly key: Uint8Array;

  constructor(secret: string) {
    this.key = new TextEncoder().encode(secret);
  }

  async sign(claims: { uid: string; rooms?: string[]; roles?: string[] }, ttlSec: number): Promise<string> {
    if (!isValidUid(claims.uid)) throw new TalkError(400, 'invalid uid');
    const payload: Record<string, unknown> = {};
    if (claims.rooms) payload.rooms = claims.rooms;
    if (claims.roles) payload.roles = claims.roles;
    return new SignJWT(payload)
      .setProtectedHeader({ alg: ALG })
      .setSubject(claims.uid)
      .setIssuedAt()
      .setExpirationTime(Math.floor(Date.now() / 1000) + ttlSec)
      .sign(this.key);
  }

  /** 校验失败抛 401 */
  async verify(token: string): Promise<TokenClaims> {
    try {
      const { payload } = await jwtVerify(token, this.key, { algorithms: [ALG] });
      const uid = payload.sub;
      if (!uid || !isValidUid(uid) || typeof payload.exp !== 'number') {
        throw new TalkError(401, 'invalid token claims');
      }
      const strings = (v: unknown) =>
        Array.isArray(v) && v.every((x) => typeof x === 'string') ? (v as string[]) : undefined;
      const rooms = strings(payload.rooms);
      const roles = strings(payload.roles);
      return { uid, exp: payload.exp, ...(rooms ? { rooms } : {}), ...(roles ? { roles } : {}) };
    } catch (err) {
      if (err instanceof TalkError) throw err;
      if (err instanceof errors.JWTExpired) throw new TalkError(401, 'token expired');
      throw new TalkError(401, 'invalid token');
    }
  }
}
