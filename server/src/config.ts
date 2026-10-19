import { randomBytes } from 'node:crypto';
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

  TALK_STUN_URLS: csv,
  TALK_TURN_URLS: csv,
  TALK_TURN_SECRET: z.string().optional(),
  TALK_TURN_TTL: z.coerce.number().int().positive().default(86400),

  TALK_JWT_SECRET: z.string().min(32).optional(),
  TALK_DEV_TOKEN_TTL: z.coerce.number().int().positive().default(86400),

  TALK_HEARTBEAT_INTERVAL: z.coerce.number().int().positive().default(15),
  TALK_IDLE_TIMEOUT: z.coerce.number().int().positive().default(45),
  TALK_RESUME_TTL: z.coerce.number().int().positive().default(60),
  TALK_REQUEST_TIMEOUT: z.coerce.number().int().positive().default(10),
  TALK_RATE_LIMIT: z.coerce.number().int().positive().default(50),
  TALK_MAX_PEERS: z.coerce.number().int().min(2).max(200).default(20),
});

export type Config = ReturnType<typeof loadConfig>;

/** turn:host:port?transport=x → stun:host:port（去重） */
function deriveStunUrls(turnUrls: string[]): string[] {
  const out = new Set<string>();
  for (const u of turnUrls) {
    const m = /^turns?:([^?]+)/.exec(u);
    if (m?.[1] && u.startsWith('turn:')) out.add(`stun:${m[1]}`);
  }
  return [...out];
}

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
  if (e.TALK_ENV === 'prod' && !e.TALK_JWT_SECRET) {
    throw new Error('配置错误：prod 环境必须设置 TALK_JWT_SECRET（至少 32 字符）');
  }
  // dev 未配置时用进程内随机密钥，重启后旧 token 失效
  const jwtSecret = e.TALK_JWT_SECRET ?? randomBytes(32).toString('hex');

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
    ice: {
      stunUrls: e.TALK_STUN_URLS.length > 0 ? e.TALK_STUN_URLS : deriveStunUrls(e.TALK_TURN_URLS),
      turnUrls: e.TALK_TURN_URLS,
      turnSecret: e.TALK_TURN_SECRET,
      turnTtl: e.TALK_TURN_TTL,
    },
    auth: {
      jwtSecret,
      jwtSecretGenerated: !e.TALK_JWT_SECRET,
      devTokenTtl: e.TALK_DEV_TOKEN_TTL,
    },
    session: {
      heartbeatInterval: e.TALK_HEARTBEAT_INTERVAL,
      idleTimeoutMs: e.TALK_IDLE_TIMEOUT * 1000,
      resumeTtlMs: e.TALK_RESUME_TTL * 1000,
      requestTimeoutMs: e.TALK_REQUEST_TIMEOUT * 1000,
      rateLimit: e.TALK_RATE_LIMIT,
    },
    room: { maxPeers: e.TALK_MAX_PEERS },
  };
}
