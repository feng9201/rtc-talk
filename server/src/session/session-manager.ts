import { randomBytes } from 'node:crypto';
import type { TokenClaims } from '../auth/token.js';
import type { Connection } from '../gateway/connection.js';

export interface ClientInfo {
  platform: string;
  version: string;
  device_id?: string | undefined;
  sdk?: string | undefined;
}

export interface Session {
  id: string;
  uid: string;
  claims: TokenClaims;
  client: ClientInfo;
  caps: unknown;
  conn: Connection | null;
  roomId: string | null;
  detachTimer: NodeJS.Timeout | undefined;
}

export type DestroyReason = 'bye' | 'timeout' | 'replaced';

export interface SessionManagerOptions {
  resumeTtlMs: number;
  /** 会话销毁时回调（离开房间等），在会话从索引移除之后调用 */
  onDestroy: (session: Session, reason: DestroyReason) => void;
}

export class SessionManager {
  private readonly byId = new Map<string, Session>();
  private readonly byUid = new Map<string, Session>();
  private readonly byConn = new WeakMap<Connection, Session>();

  constructor(private readonly opts: SessionManagerOptions) {}

  get size(): number {
    return this.byId.size;
  }

  ofConn(conn: Connection): Session | undefined {
    return this.byConn.get(conn);
  }

  ofUid(uid: string): Session | undefined {
    return this.byUid.get(uid);
  }

  /**
   * 建立或恢复会话。恢复条件：session_id 存在且属于同一 uid。
   * 返回 previousConn：被新连接取代的旧连接（调用方负责通知并关闭）。
   */
  hello(
    conn: Connection,
    client: ClientInfo,
    caps: unknown,
    resumeId: string | undefined,
  ): { session: Session; resumed: boolean; previousConn: Connection | null } {
    const uid = conn.claims.uid;
    const candidate = resumeId ? this.byId.get(resumeId) : undefined;

    if (candidate && candidate.uid === uid) {
      const previousConn = candidate.conn && candidate.conn !== conn ? candidate.conn : null;
      if (previousConn) this.byConn.delete(previousConn);
      this.clearDetach(candidate);
      candidate.conn = conn;
      candidate.claims = conn.claims;
      candidate.client = client;
      candidate.caps = caps;
      this.byConn.set(conn, candidate);
      return { session: candidate, resumed: true, previousConn };
    }

    // 同一 uid 只保留一个会话：旧会话整体作废
    let previousConn: Connection | null = null;
    const existing = this.byUid.get(uid);
    if (existing) {
      previousConn = existing.conn;
      if (previousConn) this.byConn.delete(previousConn);
      this.destroy(existing, 'replaced');
    }

    const session: Session = {
      id: `sess-${randomBytes(12).toString('base64url')}`,
      uid,
      claims: conn.claims,
      client,
      caps,
      conn,
      roomId: null,
      detachTimer: undefined,
    };
    this.byId.set(session.id, session);
    this.byUid.set(uid, session);
    this.byConn.set(conn, session);
    return { session, resumed: false, previousConn };
  }

  /** 连接断开：会话保留 resumeTtlMs，超时后销毁 */
  detach(conn: Connection): void {
    const session = this.byConn.get(conn);
    this.byConn.delete(conn);
    if (!session || session.conn !== conn) return;
    session.conn = null;
    session.detachTimer = setTimeout(() => {
      session.detachTimer = undefined;
      if (!session.conn) this.destroy(session, 'timeout');
    }, this.opts.resumeTtlMs);
    session.detachTimer.unref?.();
  }

  destroy(session: Session, reason: DestroyReason): void {
    if (this.byId.get(session.id) !== session) return;
    this.clearDetach(session);
    this.byId.delete(session.id);
    if (this.byUid.get(session.uid) === session) this.byUid.delete(session.uid);
    if (session.conn) this.byConn.delete(session.conn);
    this.opts.onDestroy(session, reason);
  }

  destroyAll(): void {
    for (const s of [...this.byId.values()]) this.destroy(s, 'bye');
  }

  private clearDetach(session: Session): void {
    if (session.detachTimer) {
      clearTimeout(session.detachTimer);
      session.detachTimer = undefined;
    }
  }
}
