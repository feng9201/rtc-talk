import type { WebSocket } from 'ws';
import type { TokenClaims } from '../auth/token.js';
import type { Logger } from '../logger.js';
import {
  errorResponse,
  notify,
  okResponse,
  parseMessage,
  request,
  TalkError,
  type Message,
  type NotifyMessage,
  type RequestMessage,
} from './protocol.js';

export const CloseCode = {
  normal: 1000,
  shutdown: 1001,
  replaced: 4001,
  kicked: 4003,
  idleTimeout: 4408,
} as const;

export interface ConnectionOptions {
  logger: Logger;
  requestTimeoutMs: number;
  idleTimeoutMs: number;
  /** 每秒允许的请求数（令牌桶容量同值） */
  rateLimit: number;
}

export interface ConnectionHandlers {
  onRequest: (conn: Connection, req: RequestMessage) => Promise<unknown>;
  onNotify: (conn: Connection, ntf: NotifyMessage) => void;
  onClose: (conn: Connection, code: number) => void;
}

interface Pending {
  resolve: (data: unknown) => void;
  reject: (err: Error) => void;
  timer: NodeJS.Timeout;
}

let nextConnId = 1;

export class Connection {
  readonly id = `conn-${nextConnId++}`;
  readonly log: Logger;
  private readonly pending = new Map<string, Pending>();
  private nextReqId = 1;
  private idleTimer: NodeJS.Timeout | undefined;
  private tokens: number;
  private lastRefill = Date.now();
  private closed = false;

  constructor(
    private readonly ws: WebSocket,
    readonly claims: TokenClaims,
    readonly remoteAddress: string,
    private readonly opts: ConnectionOptions,
    private readonly handlers: ConnectionHandlers,
  ) {
    this.log = opts.logger.child({ conn: this.id, uid: claims.uid });
    this.tokens = opts.rateLimit;
    ws.on('message', (data, isBinary) => {
      if (isBinary) {
        this.close(1003, 'binary not supported');
        return;
      }
      this.onMessage(data.toString());
    });
    ws.on('close', (code) => this.onClosed(code));
    ws.on('error', (err) => this.log.warn({ err }, 'ws error'));
    this.touch();
  }

  get isOpen(): boolean {
    return !this.closed;
  }

  notify(method: string, data: unknown): void {
    this.send(notify(method, data));
  }

  /** 服务端发起的请求（如 sfu.offer），超时或连接关闭时 reject */
  request(method: string, data: unknown, timeoutMs = this.opts.requestTimeoutMs): Promise<unknown> {
    if (this.closed) return Promise.reject(new TalkError(480, 'connection closed'));
    const id = `s-${this.nextReqId++}`;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new TalkError(500, `${method} timeout`));
      }, timeoutMs);
      this.pending.set(id, { resolve, reject, timer });
      this.send(request(id, method, data));
    });
  }

  close(code: number = CloseCode.normal, reason = ''): void {
    if (this.closed) return;
    this.ws.close(code, reason);
    // 对端不配合关闭握手时兜底
    setTimeout(() => this.ws.terminate(), 2000).unref();
  }

  private send(msg: Message): void {
    if (this.closed) return;
    this.ws.send(JSON.stringify(msg));
  }

  private touch(): void {
    if (this.idleTimer) clearTimeout(this.idleTimer);
    this.idleTimer = setTimeout(() => {
      this.log.info('idle timeout');
      this.close(CloseCode.idleTimeout, 'idle timeout');
    }, this.opts.idleTimeoutMs);
  }

  private takeToken(): boolean {
    const now = Date.now();
    const rate = this.opts.rateLimit;
    this.tokens = Math.min(rate, this.tokens + ((now - this.lastRefill) / 1000) * rate);
    this.lastRefill = now;
    if (this.tokens < 1) return false;
    this.tokens -= 1;
    return true;
  }

  private onMessage(raw: string): void {
    this.touch();
    const parsed = parseMessage(raw);
    if (!parsed.ok) {
      this.log.debug({ err: parsed.error.message }, 'bad message');
      if (parsed.reqId) this.send(errorResponse(parsed.reqId, parsed.error));
      return;
    }
    const msg = parsed.message;
    switch (msg.type) {
      case 'req':
        if (!this.takeToken()) {
          this.send(errorResponse(msg.id, new TalkError(429, 'rate limited')));
          return;
        }
        void this.dispatch(msg);
        return;
      case 'res': {
        const p = this.pending.get(msg.id);
        if (!p) return;
        this.pending.delete(msg.id);
        clearTimeout(p.timer);
        if (msg.ok) p.resolve(msg.data ?? {});
        else
          p.reject(new TalkError((msg.error?.code ?? 500) as TalkError['code'], msg.error?.msg ?? msg.error?.reason));
        return;
      }
      case 'ntf':
        if (!this.takeToken()) return;
        try {
          this.handlers.onNotify(this, msg);
        } catch (err) {
          this.log.warn({ err, method: msg.method }, 'notify handler failed');
        }
    }
  }

  private async dispatch(req: RequestMessage): Promise<void> {
    try {
      const data = await this.handlers.onRequest(this, req);
      this.send(okResponse(req.id, data ?? {}));
    } catch (err) {
      if (err instanceof TalkError) {
        const fields = { method: req.method, code: err.code, msg: err.message };
        if (err.code >= 500) this.log.error(fields, 'request rejected');
        else this.log.warn(fields, 'request rejected');
        this.send(errorResponse(req.id, err));
      } else {
        this.log.error({ err, method: req.method }, 'request handler failed');
        this.send(errorResponse(req.id, new TalkError(500)));
      }
    }
  }

  private onClosed(code: number): void {
    if (this.closed) return;
    this.closed = true;
    if (this.idleTimer) clearTimeout(this.idleTimer);
    for (const p of this.pending.values()) {
      clearTimeout(p.timer);
      p.reject(new TalkError(480, 'connection closed'));
    }
    this.pending.clear();
    this.handlers.onClose(this, code);
  }
}
