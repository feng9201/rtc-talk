import * as mediasoup from 'mediasoup';
import { loadConfig } from './config.js';
import { createHttpServer, listen } from './http/server.js';
import { createLogger } from './logger.js';
import { WorkerPool } from './media/worker-pool.js';

const startedAt = Date.now();

async function main(): Promise<void> {
  const config = loadConfig();
  const logger = createLogger({ level: config.logLevel, pretty: config.env === 'dev' && process.stdout.isTTY });

  const pool = await WorkerPool.create({
    numWorkers: config.mediasoup.numWorkers,
    workerBin: config.mediasoup.workerBin,
    logger,
    // worker 意外退出后进程直接退出，由 systemd（Restart=always）拉起
    onWorkerDied: () => setTimeout(() => process.exit(1), 1000),
  });

  const server = createHttpServer({
    logger,
    health: () => {
      const workers = pool.status();
      return {
        ok: workers.every((w) => !w.closed),
        env: config.env,
        rev: config.buildRev,
        uptime: Math.round((Date.now() - startedAt) / 1000),
        mediasoup: { version: mediasoup.version, workers },
      };
    },
  });
  await listen(server, config.http.host, config.http.port);
  logger.info(
    { host: config.http.host, port: config.http.port, env: config.env, rev: config.buildRev },
    'talk-server listening',
  );

  let stopping = false;
  const shutdown = (signal: string) => {
    if (stopping) return;
    stopping = true;
    logger.info({ signal }, 'shutting down');
    server.close();
    pool.close();
    setTimeout(() => process.exit(0), 500).unref();
  };
  process.on('SIGTERM', () => shutdown('SIGTERM'));
  process.on('SIGINT', () => shutdown('SIGINT'));
}

main().catch((err: unknown) => {
  console.error(err);
  process.exit(1);
});
