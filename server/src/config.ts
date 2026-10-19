import { z } from 'zod';

const csv = z
  .string()
  .optional()
  .transform((v) =>
    (v ?? '')
      .split(',')
      .map((s) => s.trim())
      .filter(Boolean),
  );

const port = z.coerce.number().int().min(1).max(65535);

const EnvSchema = z.object({
  TALK_ENV: z.enum(['dev', 'prod']).default('prod'),
  TALK_LOG_LEVEL: z.enum(['trace', 'debug', 'info', 'warn', 'error', 'fatal']).default('info'),
  TALK_BUILD_REV: z.string().default('dev'),

  TALK_HTTP_HOST: z.string().default('0.0.0.0'),
  TALK_HTTP_PORT: port.default(8080),

  TALK_NUM_WORKERS: z.coerce.number().int().min(1).max(64).default(1),
  MEDIASOUP_WORKER_BIN: z.string().optional(),
  TALK_RTC_LISTEN_IP: z.string().default('0.0.0.0'),
  TALK_RTC_ANNOUNCED_ADDRESS: z.string().optional(),
  TALK_RTC_PORT: port.default(44444),

  TALK_TURN_URLS: csv,
  TALK_TURN_SECRET: z.string().optional(),
  TALK_TURN_TTL: z.coerce.number().int().positive().default(86400),
});

export type Config = ReturnType<typeof loadConfig>;

export function loadConfig(env: NodeJS.ProcessEnv = process.env) {
  const parsed = EnvSchema.safeParse(env);
  if (!parsed.success) {
    const issues = parsed.error.issues.map((i) => `${i.path.join('.')}: ${i.message}`).join('; ');
    throw new Error(`配置错误：${issues}`);
  }
  const e = parsed.data;
  if (e.TALK_TURN_URLS.length > 0 && !e.TALK_TURN_SECRET) {
    throw new Error('配置错误：设置了 TALK_TURN_URLS 但缺少 TALK_TURN_SECRET');
  }

  return {
    env: e.TALK_ENV,
    logLevel: e.TALK_LOG_LEVEL,
    buildRev: e.TALK_BUILD_REV,
    http: { host: e.TALK_HTTP_HOST, port: e.TALK_HTTP_PORT },
    mediasoup: {
      numWorkers: e.TALK_NUM_WORKERS,
      workerBin: e.MEDIASOUP_WORKER_BIN,
      listenIp: e.TALK_RTC_LISTEN_IP,
      announcedAddress: e.TALK_RTC_ANNOUNCED_ADDRESS,
      port: e.TALK_RTC_PORT,
    },
    turn: { urls: e.TALK_TURN_URLS, secret: e.TALK_TURN_SECRET, ttl: e.TALK_TURN_TTL },
  };
}
