import { z } from 'zod';

export const PROTOCOL_VERSION = 1;
export const SUBPROTOCOL = 'talk.v1.json';
export const WS_PATH = '/talk/v1/ws';

export const ErrorReason = {
  400: 'bad_request',
  401: 'unauthorized',
  403: 'forbidden',
  404: 'not_found',
  409: 'conflict',
  429: 'limited',
  480: 'unavailable',
  486: 'busy',
  487: 'cancelled',
  488: 'not_acceptable',
  500: 'internal',
  501: 'not_implemented',
  503: 'overloaded',
} as const;

export type ErrorCode = keyof typeof ErrorReason;

export class TalkError extends Error {
  constructor(
    readonly code: ErrorCode,
    message?: string,
  ) {
    super(message ?? ErrorReason[code]);
    this.name = 'TalkError';
  }

  get reason(): string {
    return ErrorReason[this.code];
  }

  toWire(): WireError {
    return { code: this.code, reason: this.reason, msg: this.message };
  }
}

export interface WireError {
  code: number;
  reason: string;
  msg?: string;
}

export interface RequestMessage {
  v: number;
  type: 'req';
  id: string;
  method: string;
  data: unknown;
}

export interface ResponseMessage {
  v: number;
  type: 'res';
  id: string;
  ok: boolean;
  data?: unknown;
  error?: WireError;
}

export interface NotifyMessage {
  v: number;
  type: 'ntf';
  method: string;
  data: unknown;
}

export type Message = RequestMessage | ResponseMessage | NotifyMessage;

const method = z.string().regex(/^[a-z][a-zA-Z0-9]*\.[a-z][a-zA-Z0-9]*$/);
const id = z.string().min(1).max(64);

// 未知字段一律忽略（协议 2.2，向前兼容）
const MessageSchema = z.discriminatedUnion('type', [
  z.object({ v: z.number().int(), type: z.literal('req'), id, method, data: z.unknown().optional() }),
  z.object({
    v: z.number().int(),
    type: z.literal('res'),
    id,
    ok: z.boolean(),
    data: z.unknown().optional(),
    error: z.object({ code: z.number().int(), reason: z.string(), msg: z.string().optional() }).optional(),
  }),
  z.object({ v: z.number().int(), type: z.literal('ntf'), method, data: z.unknown().optional() }),
]);

export type ParseResult =
  | { ok: true; message: Message }
  // reqId 能取到时用于回 400 应答
  | { ok: false; error: TalkError; reqId?: string };

export function parseMessage(raw: string): ParseResult {
  let json: unknown;
  try {
    json = JSON.parse(raw);
  } catch {
    return { ok: false, error: new TalkError(400, 'invalid json') };
  }
  const parsed = MessageSchema.safeParse(json);
  if (!parsed.success) {
    const reqId =
      typeof json === 'object' && json !== null && (json as { type?: unknown }).type === 'req'
        ? String((json as { id?: unknown }).id ?? '')
        : undefined;
    return { ok: false, error: new TalkError(400, 'invalid message envelope'), ...(reqId ? { reqId } : {}) };
  }
  const m = parsed.data;
  if (m.v !== PROTOCOL_VERSION) {
    return {
      ok: false,
      error: new TalkError(400, `unsupported protocol version ${m.v}`),
      ...(m.type === 'req' ? { reqId: m.id } : {}),
    };
  }
  const message: Message =
    m.type === 'req'
      ? { v: m.v, type: 'req', id: m.id, method: m.method, data: m.data ?? {} }
      : m.type === 'ntf'
        ? { v: m.v, type: 'ntf', method: m.method, data: m.data ?? {} }
        : {
            v: m.v,
            type: 'res',
            id: m.id,
            ok: m.ok,
            ...(m.data !== undefined ? { data: m.data } : {}),
            ...(m.error ? { error: m.error } : {}),
          };
  return { ok: true, message };
}

export function request(reqId: string, method: string, data: unknown): RequestMessage {
  return { v: PROTOCOL_VERSION, type: 'req', id: reqId, method, data };
}

export function okResponse(reqId: string, data: unknown = {}): ResponseMessage {
  return { v: PROTOCOL_VERSION, type: 'res', id: reqId, ok: true, data };
}

export function errorResponse(reqId: string, err: TalkError): ResponseMessage {
  return { v: PROTOCOL_VERSION, type: 'res', id: reqId, ok: false, error: err.toWire() };
}

export function notify(method: string, data: unknown): NotifyMessage {
  return { v: PROTOCOL_VERSION, type: 'ntf', method, data };
}

/** 用 zod schema 校验请求 data，失败抛 400 */
export function parseData<T extends z.ZodType>(schema: T, data: unknown): z.output<T> {
  const r = schema.safeParse(data);
  if (!r.success) {
    const detail = r.error.issues.map((i) => `${i.path.join('.') || 'data'}: ${i.message}`).join('; ');
    throw new TalkError(400, detail);
  }
  return r.data;
}
