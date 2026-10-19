import { createServer, type IncomingMessage, type Server, type ServerResponse } from 'node:http';
import type { Logger } from '../logger.js';
import { TalkError } from '../gateway/protocol.js';

export interface HealthInfo {
  ok: boolean;
  [key: string]: unknown;
}

export interface RouteResult {
  status: number;
  body: unknown;
}

/** GET 路由；抛 TalkError 时按其 code 返回 */
export type Route = (url: URL, req: IncomingMessage) => RouteResult | Promise<RouteResult>;

export interface HttpServerOptions {
  logger: Logger;
  health: () => HealthInfo;
  routes?: Record<string, Route>;
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
  const routes: Record<string, Route> = {
    ...opts.routes,
    '/healthz': () => {
      const info = opts.health();
      return { status: info.ok ? 200 : 503, body: info };
    },
  };
  return createServer((req: IncomingMessage, res: ServerResponse) => {
    const url = new URL(req.url ?? '/', 'http://localhost');
    const route = req.method === 'GET' ? routes[url.pathname] : undefined;
    if (!route) {
      sendJson(res, 404, { error: 'not_found' });
      return;
    }
    Promise.resolve()
      .then(() => route(url, req))
      .then(
        (r) => sendJson(res, r.status, r.body),
        (err: unknown) => {
          if (err instanceof TalkError) {
            sendJson(res, err.code, { error: err.reason, msg: err.message });
          } else {
            opts.logger.error({ err, path: url.pathname }, 'http route failed');
            sendJson(res, 500, { error: 'internal' });
          }
        },
      );
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
