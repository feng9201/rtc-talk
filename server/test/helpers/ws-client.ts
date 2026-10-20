import { WebSocket } from 'ws';
import { SUBPROTOCOL, WS_PATH } from '../../src/gateway/protocol.js';

interface Ntf {
  method: string;
  data: Record<string, unknown>;
}

type Waiter = { method: string; pred: (d: any) => boolean; resolve: (d: any) => void };

function waitFor(list: Ntf[], queued: Waiter[], method: string, pred: (d: any) => boolean, timeoutMs: number) {
  const i = list.findIndex((n) => n.method === method && pred(n.data));
  if (i >= 0) return Promise.resolve(list.splice(i, 1)[0]!.data);
  return new Promise((resolve, reject) => {
    const w = { method, pred, resolve };
    queued.push(w);
    setTimeout(() => {
      const j = queued.indexOf(w);
      if (j >= 0) {
        queued.splice(j, 1);
        reject(new Error(`timeout waiting ${method}`));
      }
    }, timeoutMs);
  });
}

export interface TestClient {
  ws: WebSocket;
  req(
    method: string,
    data?: unknown,
  ): Promise<{ ok: boolean; data?: any; error?: { code: number; reason: string; msg?: string } }>;
  ntf(method: string, data?: unknown): void;
  waitNtf(method: string, pred?: (data: any) => boolean, timeoutMs?: number): Promise<any>;
  /** 服务端发来的 req（如 room.mediaMode）。默认自动回空应答 */
  waitReq(method: string, pred?: (data: any) => boolean, timeoutMs?: number): Promise<any>;
  closed: Promise<number>;
}

export function connect(base: string, token: string): Promise<TestClient> {
  const ws = new WebSocket(`${base.replace(/^http/, 'ws')}${WS_PATH}?token=${token}`, SUBPROTOCOL);
  let seq = 1;
  const pending = new Map<string, (msg: any) => void>();
  const ntfs: Ntf[] = [];
  const reqs: Ntf[] = [];
  const waiters: Waiter[] = [];
  const reqWaiters: Waiter[] = [];

  function take(list: Ntf[], queued: Waiter[], method: string, data: any) {
    const i = queued.findIndex((w) => w.method === method && w.pred(data));
    if (i >= 0) queued.splice(i, 1)[0]!.resolve(data);
    else list.push({ method, data });
  }

  ws.on('message', (raw) => {
    const msg = JSON.parse(raw.toString());
    if (msg.type === 'res') {
      pending.get(msg.id)?.(msg);
      pending.delete(msg.id);
    } else if (msg.type === 'ntf') {
      take(ntfs, waiters, msg.method, msg.data);
    } else if (msg.type === 'req') {
      take(reqs, reqWaiters, msg.method, msg.data);
      ws.send(JSON.stringify({ v: 1, type: 'res', id: msg.id, ok: true, data: {} }));
    }
  });

  const closed = new Promise<number>((resolve) => ws.on('close', (code) => resolve(code)));

  const client: TestClient = {
    ws,
    closed,
    req(method, data = {}) {
      const id = `c-${seq++}`;
      return new Promise((resolve) => {
        pending.set(id, resolve);
        ws.send(JSON.stringify({ v: 1, type: 'req', id, method, data }));
      });
    },
    ntf(method, data = {}) {
      ws.send(JSON.stringify({ v: 1, type: 'ntf', method, data }));
    },
    waitNtf(method, pred = () => true, timeoutMs = 2000) {
      return waitFor(ntfs, waiters, method, pred, timeoutMs);
    },
    waitReq(method, pred = () => true, timeoutMs = 2000) {
      return waitFor(reqs, reqWaiters, method, pred, timeoutMs);
    },
  };
  return new Promise((resolve, reject) => {
    ws.once('open', () => resolve(client));
    ws.once('unexpected-response', (_req, res) => reject(new Error(`http ${res.statusCode}`)));
    ws.once('error', reject);
  });
}
