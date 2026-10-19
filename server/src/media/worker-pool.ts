import { existsSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import * as mediasoup from 'mediasoup';
import type { types as ms } from 'mediasoup';
import type { Logger } from '../logger.js';

export interface WorkerPoolOptions {
  numWorkers: number;
  workerBin: string | undefined;
  logger: Logger;
  onWorkerDied: (err: Error) => void;
}

export interface WorkerStatus {
  pid: number;
  closed: boolean;
}

// worker 二进制与 mediasoup 的 Node 端走同一套 FlatBuffers 协议，版本必须一致
export function checkWorkerBinVersion(workerBin: string): void {
  if (!existsSync(workerBin)) {
    throw new Error(
      `mediasoup worker 不存在：${workerBin}（先执行 npm install 或 scripts/fetch-mediasoup-worker.mjs）`,
    );
  }
  const versionFile = join(dirname(workerBin), 'VERSION');
  if (!existsSync(versionFile)) return;
  const binVersion = readFileSync(versionFile, 'utf8').trim();
  if (binVersion !== mediasoup.version) {
    throw new Error(
      `mediasoup worker 版本 ${binVersion} 与 mediasoup ${mediasoup.version} 不一致，重新执行 scripts/fetch-mediasoup-worker.mjs`,
    );
  }
}

export class WorkerPool {
  private readonly workers: ms.Worker[] = [];
  private next = 0;

  private constructor(private readonly opts: WorkerPoolOptions) {}

  static async create(opts: WorkerPoolOptions): Promise<WorkerPool> {
    if (opts.workerBin) checkWorkerBinVersion(opts.workerBin);
    const pool = new WorkerPool(opts);
    for (let i = 0; i < opts.numWorkers; i++) {
      await pool.spawn(i);
    }
    return pool;
  }

  private async spawn(index: number): Promise<void> {
    const { logger } = this.opts;
    const worker = await mediasoup.createWorker({
      logLevel: 'warn',
      ...(this.opts.workerBin ? { workerBin: this.opts.workerBin } : {}),
      appData: { index },
    });
    worker.on('died', (err) => {
      logger.fatal({ pid: worker.pid, err }, 'mediasoup worker died');
      this.opts.onWorkerDied(err);
    });
    this.workers.push(worker);
    logger.info({ index, pid: worker.pid, version: mediasoup.version }, 'mediasoup worker started');
  }

  list(): readonly ms.Worker[] {
    return this.workers;
  }

  /** 轮询分配 worker */
  pick(): ms.Worker {
    const worker = this.workers[this.next % this.workers.length];
    if (!worker) throw new Error('worker pool is empty');
    this.next++;
    return worker;
  }

  status(): WorkerStatus[] {
    return this.workers.map((w) => ({ pid: w.pid, closed: w.closed }));
  }

  close(): void {
    for (const w of this.workers) w.close();
  }
}
