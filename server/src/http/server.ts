import { createServer, type IncomingMessage, type Server, type ServerResponse } from 'node:http';
import type { Logger } from '../logger.js';

export interface HealthInfo {
  ok: boolean;
  [key: string]: unknown;
}

export interface HttpServerOptions {
  logger: Logger;
  health: () => HealthInfo;
}

function sendJson(res: ServerResponse, status: number, body: unknown): void {
  const data = JSON.stringify(body);
  res.writeHead(status, {
    'content-type': 'application/json; charset=utf-8',
    'content-length': Buffer.byteLength(data),
    'cache-control': 'no-store',
  });
  res.end(data);
}

export function createHttpServer(opts: HttpServerOptions): Server {
  return createServer((req: IncomingMessage, res: ServerResponse) => {
    const path = (req.url ?? '/').split('?')[0];
    if (req.method === 'GET' && path === '/healthz') {
      const info = opts.health();
      sendJson(res, info.ok ? 200 : 503, info);
      return;
    }
    sendJson(res, 404, { error: 'not_found' });
  });
}

export function listen(server: Server, host: string, port: number): Promise<void> {
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, host, () => {
      server.off('error', reject);
      resolve();
    });
  });
}
