import { WebSocket } from 'ws';

export interface WireError {
  code: number;
  reason: string;
  msg?: string;
}

export class RequestError extends Error {
  constructor(
    readonly method: string,
    readonly error: WireError,
  ) {
    super(`${method} failed: ${error.code} ${error.reason}${error.msg ? ` (${error.msg})` : ''}`);
  }
}

type NotifyListener = (method: string, data: Record<string, unknown>) => void;

/** 协议 v1 的最小客户端：req/res 关联、ntf 分发、服务端 req 一律回 501 */
export class SignalClient {
  private seq = 1;
  private readonly pending = new Map<
    string,
    { method: string; resolve: (d: unknown) => void; reject: (e: Error) => void; timer: NodeJS.Timeout }
  >();
  private readonly listeners = new Set<NotifyListener>();
  private onRequest?: (method: string, data: Record<string, unknown>) => Promise<unknown>;
  readonly closed: Promise<{ code: number; reason: string }>;

  private constructor(private readonly ws: WebSocket) {
    ws.on('message', (raw) => this.onMessage(raw.toString()));
    this.closed = new Promise((resolve) => {
      ws.on('close', (code, reason) => {
        for (const p of this.pending.values()) {
          clearTimeout(p.timer);
          p.reject(new Error(`${p.method}: connection closed`));
        }
        this.pending.clear();
        resolve({ code, reason: reason.toString() });
      });
    });
  }

  static connect(wsUrl: string, token: string, timeoutMs = 10_000): Promise<SignalClient> {
    const url = new URL(wsUrl);
    url.searchParams.set('token', token);
    const ws = new WebSocket(url, 'talk.v1.json', { handshakeTimeout: timeoutMs });
    return new Promise((resolve, reject) => {
      ws.once('open', () => resolve(new SignalClient(ws)));
      ws.once('unexpected-response', (_req, res) => reject(new Error(`ws upgrade rejected: HTTP ${res.statusCode}`)));
      ws.once('error', reject);
    });
  }

  /** 应答服务端请求（如 sfu.offer）。未注册时回 501。 */
  handleRequest(handler: (method: string, data: Record<string, unknown>) => Promise<unknown>): void {
    this.onRequest = handler;
  }

  onNotify(listener: NotifyListener): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  request<T = Record<string, unknown>>(method: string, data: unknown = {}, timeoutMs = 10_000): Promise<T> {
    const id = `c-${this.seq++}`;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`${method}: timeout`));
      }, timeoutMs);
      this.pending.set(id, { method, resolve: resolve as (d: unknown) => void, reject, timer });
      this.ws.send(JSON.stringify({ v: 1, type: 'req', id, method, data }));
    });
  }

  close(code = 1000): void {
    this.ws.close(code);
  }

  terminate(): void {
    this.ws.terminate();
  }

  private onMessage(raw: string): void {
    let msg: { type?: string; id?: string; method?: string; ok?: boolean; data?: unknown; error?: WireError };
    try {
      msg = JSON.parse(raw);
    } catch {
      return;
    }
    if (msg.type === 'res' && msg.id) {
      const p = this.pending.get(msg.id);
      if (!p) return;
      this.pending.delete(msg.id);
      clearTimeout(p.timer);
      if (msg.ok) p.resolve(msg.data ?? {});
      else p.reject(new RequestError(p.method, msg.error ?? { code: 500, reason: 'internal' }));
    } else if (msg.type === 'ntf' && msg.method) {
      for (const l of this.listeners) l(msg.method, (msg.data ?? {}) as Record<string, unknown>);
    } else if (msg.type === 'req' && msg.id && msg.method) {
      const id = msg.id;
      const method = msg.method;
      const handler = this.onRequest;
      if (!handler) {
        this.ws.send(
          JSON.stringify({ v: 1, type: 'res', id, ok: false, error: { code: 501, reason: 'not_implemented' } }),
        );
        return;
      }
      void handler(method, (msg.data ?? {}) as Record<string, unknown>).then(
        (data) => this.ws.send(JSON.stringify({ v: 1, type: 'res', id, ok: true, data: data ?? {} })),
        (err: Error) =>
          this.ws.send(
            JSON.stringify({
              v: 1,
              type: 'res',
              id,
              ok: false,
              error: { code: 500, reason: 'internal', msg: err.message },
            }),
          ),
      );
    }
  }
}
