import { createApp } from './app.js';
import { loadConfig } from './config.js';
import { createLogger } from './logger.js';
import { MediaNode } from './media/media-node.js';

async function main(): Promise<void> {
  const config = loadConfig();
  const logger = createLogger({ level: config.logLevel, pretty: config.env === 'dev' && process.stdout.isTTY });
  if (config.auth.jwtSecretGenerated) logger.warn('TALK_JWT_SECRET 未设置，使用随机密钥，重启后 token 失效');

  const media = await MediaNode.create({
    numWorkers: config.mediasoup.numWorkers,
    workerBin: config.mediasoup.workerBin,
    listenIp: config.mediasoup.listenIp,
    announcedAddress: config.mediasoup.announcedAddress,
    port: config.mediasoup.port,
    logger,
    onWorkerDied: (err) => {
      // worker 挂了所有 Router 都失效，无法原地恢复；延长退出时间，让 /healthz 的 503
      // 先把流量摘掉，再由 systemd 重启（Restart=always）
      logger.fatal({ err }, 'mediasoup worker died, restarting in 3s');
      setTimeout(() => process.exit(1), 3000).unref();
    },
  });

  const app = createApp({
    config,
    logger,
    media,
    health: () => {
      const info = media.status();
      return {
        ok: info.workers.every((w) => !w.closed),
        mediasoup: { version: info.version, workers: info.workers, rooms: info.rooms },
      };
    },
  });
  await app.listen();
  logger.info(
    { host: config.http.host, port: config.http.port, env: config.env, rev: config.buildRev },
    'talk-server listening',
  );

  let stopping = false;
  const shutdown = (signal: string) => {
    if (stopping) return;
    stopping = true;
    logger.info({ signal }, 'shutting down');
    app.close();
    media.close();
    setTimeout(() => process.exit(0), 500).unref();
  };
  process.on('SIGTERM', () => shutdown('SIGTERM'));
  process.on('SIGINT', () => shutdown('SIGINT'));
}

main().catch((err: unknown) => {
  console.error(err);
  process.exit(1);
});
