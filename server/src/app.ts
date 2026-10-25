import type { Server } from 'node:http';
import type { AddressInfo } from 'node:net';
import { isValidUid, TokenService } from './auth/token.js';
import type { Config } from './config.js';
import { TalkError } from './gateway/protocol.js';
import { WsGateway } from './gateway/ws-gateway.js';
import { createHttpServer, listen, type HealthInfo, type Route } from './http/server.js';
import type { Logger } from './logger.js';
import type { MediaNode } from './media/media-node.js';
import { renderMetrics, type Sample } from './metrics.js';
import { ROOM_ID_RE } from './room/room-manager.js';
import { Signaling } from './signaling.js';

export interface AppOptions {
  config: Config;
  logger: Logger;
  /** 不传则只有信令，sfu.* 返回 501（单测用） */
  media?: MediaNode;
  /** 额外的健康信息（如 mediasoup worker 状态），ok=false 时 /healthz 返回 503 */
  health?: () => HealthInfo;
}

export interface App {
  server: Server;
  signaling: Signaling;
  gateway: WsGateway;
  tokens: TokenService;
  listen(): Promise<AddressInfo>;
  close(): void;
}

function devTokenRoute(tokens: TokenService, maxTtl: number): Route {
  return async (url) => {
    const uid = url.searchParams.get('uid') ?? '';
    if (!isValidUid(uid)) throw new TalkError(400, 'invalid uid');
    const roomsParam = url.searchParams.get('rooms');
    const rooms = roomsParam ? roomsParam.split(',').filter(Boolean) : undefined;
    if (rooms?.some((r) => !ROOM_ID_RE.test(r))) throw new TalkError(400, 'invalid rooms');
    const ttl = Math.min(Number(url.searchParams.get('ttl') ?? maxTtl) || maxTtl, maxTtl);
    const token = await tokens.sign({ uid, ...(rooms ? { rooms } : {}) }, ttl);
    return { status: 200, body: { token, uid, exp: Math.floor(Date.now() / 1000) + ttl } };
  };
}

export function createApp(opts: AppOptions): App {
  const { config, logger } = opts;
  const startedAt = Date.now();
  const tokens = new TokenService(config.auth.jwtSecret);
  const signaling = new Signaling({
    logger,
    ice: config.ice,
    heartbeatInterval: config.session.heartbeatInterval,
    resumeTtlMs: config.session.resumeTtlMs,
    maxPeersLimit: config.room.maxPeers,
    ringTimeoutMs: config.call.ringTimeoutMs,
    p2pTimeoutMs: config.call.p2pTimeoutMs,
    ...(opts.media ? { media: opts.media } : {}),
  });
  opts.media?.setAnswerHandler((uid, offer) => signaling.answerRecvOffer(uid, offer));

  const routes: Record<string, Route> = {};
  if (config.env === 'dev') routes['/dev/token'] = devTokenRoute(tokens, config.auth.devTokenTtl);
  // Prometheus 拉取端点；无第三方客户端，按当前状态现算
  routes['/metrics'] = () => {
    const media = opts.media?.status();
    const stats = signaling.statsSnapshot();
    const samples: Sample[] = [
      { name: 'talk_up', help: 'Service is up', type: 'gauge', value: 1 },
      {
        name: 'talk_uptime_seconds',
        help: 'Process uptime in seconds',
        type: 'gauge',
        value: Math.round((Date.now() - startedAt) / 1000),
      },
      { name: 'talk_process_rss_bytes', help: 'Resident set size', type: 'gauge', value: process.memoryUsage().rss },
      { name: 'talk_signaling_connections', help: 'Open websocket connections', type: 'gauge', value: gateway.size },
      { name: 'talk_sessions', help: 'Active signaling sessions', type: 'gauge', value: signaling.sessions.size },
      { name: 'talk_rooms', help: 'Active rooms', type: 'gauge', value: signaling.rooms.size },
      {
        name: 'talk_sfu_client_stats_reports_total',
        help: 'Client sfu.stats reports',
        type: 'counter',
        value: stats.reports,
      },
      {
        name: 'talk_sfu_client_rtt_ms_avg',
        help: 'Average client-reported RTT in ms',
        type: 'gauge',
        value: stats.rtt_avg_ms,
      },
      {
        name: 'talk_sfu_client_loss_ratio_avg',
        help: 'Average client-reported packet loss ratio',
        type: 'gauge',
        value: stats.loss_avg,
      },
      {
        name: 'talk_sfu_client_bitrate_avg',
        help: 'Average client-reported bitrate',
        type: 'gauge',
        value: stats.bitrate_avg,
      },
    ];
    if (media) {
      samples.push(
        { name: 'talk_media_rooms', help: 'Rooms with a mediasoup router', type: 'gauge', value: media.rooms },
        { name: 'talk_media_workers', help: 'mediasoup workers', type: 'gauge', value: media.workers.length },
        {
          name: 'talk_media_workers_closed',
          help: 'Closed mediasoup workers',
          type: 'gauge',
          value: media.workers.filter((w) => w.closed).length,
        },
      );
    }
    return {
      status: 200,
      contentType: 'text/plain; version=0.0.4; charset=utf-8',
      body: renderMetrics(samples),
    };
  };

  const server = createHttpServer({
    logger,
    routes,
    health: () => {
      const extra = opts.health?.() ?? { ok: true };
      return {
        ...extra,
        ok: extra.ok,
        env: config.env,
        rev: config.buildRev,
        uptime: Math.round((Date.now() - startedAt) / 1000),
        signaling: {
          connections: gateway.size,
          sessions: signaling.sessions.size,
          rooms: signaling.rooms.size,
        },
      };
    },
  });
  const gateway = new WsGateway(server, {
    logger,
    tokens,
    handlers: signaling.connectionHandlers,
    requestTimeoutMs: config.session.requestTimeoutMs,
    idleTimeoutMs: config.session.idleTimeoutMs,
    rateLimit: config.session.rateLimit,
  });
  return {
    server,
    signaling,
    gateway,
    tokens,
    async listen() {
      await listen(server, config.http.host, config.http.port);
      return server.address() as AddressInfo;
    },
    close() {
      gateway.close();
      signaling.close();
      server.close();
    },
  };
}
