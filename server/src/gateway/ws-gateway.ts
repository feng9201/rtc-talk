import type { IncomingMessage, Server } from 'node:http';
import type { Duplex } from 'node:stream';
import { WebSocketServer } from 'ws';
import type { TokenClaims, TokenService } from '../auth/token.js';
import type { Logger } from '../logger.js';
import { CloseCode, Connection, type ConnectionHandlers, type ConnectionOptions } from './connection.js';
import { SUBPROTOCOL, TalkError, WS_PATH } from './protocol.js';

export interface WsGatewayOptions extends ConnectionOptions {
  tokens: TokenService;
  handlers: ConnectionHandlers;
  maxPayload?: number;
}

function rejectUpgrade(socket: Duplex, status: number, text: string): void {
  socket.write(`HTTP/1.1 ${status} ${text}\r\nConnection: close\r\nContent-Length: 0\r\n\r\n`);
  socket.destroy();
}

function extractToken(req: IncomingMessage, url: URL): string | undefined {
  const q = url.searchParams.get('token');
  if (q) return q;
  const auth = req.headers.authorization;
  return auth?.startsWith('Bearer ') ? auth.slice(7) : undefined;
}

export class WsGateway {
  private readonly wss: WebSocketServer;
  private readonly conns = new Set<Connection>();
  private readonly log: Logger;

  constructor(
    server: Server,
    private readonly opts: WsGatewayOptions,
  ) {
    this.log = opts.logger.child({ mod: 'ws' });
    this.wss = new WebSocketServer({
      noServer: true,
      maxPayload: opts.maxPayload ?? 256 * 1024,
      handleProtocols: (protocols) => (protocols.has(SUBPROTOCOL) ? SUBPROTOCOL : false),
    });
    server.on('upgrade', (req, socket, head) => {
      void this.onUpgrade(req, socket, head);
    });
  }

  get size(): number {
    return this.conns.size;
  }

  private async onUpgrade(req: IncomingMessage, socket: Duplex, head: Buffer): Promise<void> {
    const url = new URL(req.url ?? '/', 'http://localhost');
    if (url.pathname !== WS_PATH) return rejectUpgrade(socket, 404, 'Not Found');

    const offered = req.headers['sec-websocket-protocol'];
    if (offered && !offered.split(',').some((p) => p.trim() === SUBPROTOCOL)) {
      return rejectUpgrade(socket, 400, 'Bad Request');
    }

    const token = extractToken(req, url);
    let claims: TokenClaims;
    try {
      if (!token) throw new TalkError(401, 'missing token');
      claims = await this.opts.tokens.verify(token);
    } catch (err) {
      this.log.info({ err: (err as Error).message, ip: req.socket.remoteAddress }, 'ws auth failed');
      return rejectUpgrade(socket, 401, 'Unauthorized');
    }

    this.wss.handleUpgrade(req, socket, head, (ws) => {
      const remote = String(req.headers['x-forwarded-for'] ?? req.socket.remoteAddress ?? '');
      const conn = new Connection(ws, claims, remote, this.opts, {
        onRequest: this.opts.handlers.onRequest,
        onNotify: this.opts.handlers.onNotify,
        onClose: (c, code) => {
          this.conns.delete(c);
          c.log.info({ code }, 'ws closed');
          this.opts.handlers.onClose(c, code);
        },
      });
      this.conns.add(conn);
      conn.log.info({ ip: remote }, 'ws connected');
    });
  }

  close(): void {
    for (const c of this.conns) c.close(CloseCode.shutdown, 'server shutdown');
    this.wss.close();
  }
}
