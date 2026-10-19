import { WebSocket } from 'ws';
import { SUBPROTOCOL, WS_PATH } from '../../src/gateway/protocol.js';

interface Ntf {
  method: string;
  data: Record<string, unknown>;
}

export interface TestClient {
  ws: WebSocket;
  req(method: string, data?: unknown): Promise<{ ok: boolean; data?: any; error?: { code: number; reason: string } }>;
  waitNtf(method: string, pred?: (data: any) => boolean, timeoutMs?: number): Promise<any>;
  closed: Promise<number>;
}

export function connect(base: string, token: string): Promise<TestClient> {
  const ws = new WebSocket(`${base.replace(/^http/, 'ws')}${WS_PATH}?token=${token}`, SUBPROTOCOL);
  let seq = 1;
  const pending = new Map<string, (msg: any) => void>();
  const ntfs: Ntf[] = [];
  const waiters: Array<{ method: string; pred: (d: any) => boolean; resolve: (d: any) => void }> = [];

  ws.on('message', (raw) => {
    const msg = JSON.parse(raw.toString());
    if (msg.type === 'res') {
      pending.get(msg.id)?.(msg);
      pending.delete(msg.id);
    } else if (msg.type === 'ntf') {
      const i = waiters.findIndex((w) => w.method === msg.method && w.pred(msg.data));
      if (i >= 0) waiters.splice(i, 1)[0]!.resolve(msg.data);
      else ntfs.push({ method: msg.method, data: msg.data });
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
    waitNtf(method, pred = () => true, timeoutMs = 2000) {
      const i = ntfs.findIndex((n) => n.method === method && pred(n.data));
      if (i >= 0) return Promise.resolve(ntfs.splice(i, 1)[0]!.data);
      return new Promise((resolve, reject) => {
        const w = { method, pred, resolve };
        waiters.push(w);
        setTimeout(() => {
          const j = waiters.indexOf(w);
          if (j >= 0) {
            waiters.splice(j, 1);
            reject(new Error(`timeout waiting ${method}`));
          }
        }, timeoutMs);
      });
    },
  };
  return new Promise((resolve, reject) => {
    ws.once('open', () => resolve(client));
    ws.once('unexpected-response', (_req, res) => reject(new Error(`http ${res.statusCode}`)));
    ws.once('error', reject);
  });
}
