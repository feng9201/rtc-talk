import { pino, type Logger } from 'pino';

export type { Logger };

export function createLogger(opts: { level: string; pretty: boolean }): Logger {
  return pino({
    level: opts.level,
    base: undefined,
    timestamp: pino.stdTimeFunctions.isoTime,
    ...(opts.pretty
      ? { transport: { target: 'pino-pretty', options: { translateTime: 'SYS:HH:MM:ss.l', ignore: 'pid,hostname' } } }
      : {}),
  });
}
