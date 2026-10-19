import { describe, expect, it } from 'vitest';
import { z } from 'zod';
import { errorResponse, okResponse, parseData, parseMessage, TalkError } from '../src/gateway/protocol.js';

describe('parseMessage', () => {
  it('parses req and defaults data to {}', () => {
    const r = parseMessage('{"v":1,"type":"req","id":"c-1","method":"room.join"}');
    expect(r).toEqual({ ok: true, message: { v: 1, type: 'req', id: 'c-1', method: 'room.join', data: {} } });
  });

  it('ignores unknown fields', () => {
    const r = parseMessage('{"v":1,"type":"ntf","method":"p2p.state","data":{"a":1},"extra":true}');
    expect(r.ok && r.message).toEqual({ v: 1, type: 'ntf', method: 'p2p.state', data: { a: 1 } });
  });

  it('parses error res', () => {
    const r = parseMessage('{"v":1,"type":"res","id":"s-1","ok":false,"error":{"code":409,"reason":"conflict"}}');
    expect(r.ok && r.message.type === 'res' && r.message.error?.code).toBe(409);
  });

  it('rejects invalid json', () => {
    const r = parseMessage('{oops');
    expect(r.ok).toBe(false);
    expect(!r.ok && r.error.code).toBe(400);
  });

  it('rejects bad envelope but keeps req id for the error response', () => {
    const r = parseMessage('{"v":1,"type":"req","id":"c-9","method":"BAD METHOD"}');
    expect(r).toMatchObject({ ok: false, reqId: 'c-9' });
  });

  it('rejects unsupported version', () => {
    const r = parseMessage('{"v":2,"type":"req","id":"c-1","method":"room.join"}');
    expect(r).toMatchObject({ ok: false, reqId: 'c-1' });
    expect(!r.ok && r.error.message).toMatch(/version 2/);
  });
});

describe('responses', () => {
  it('builds ok and error responses', () => {
    expect(okResponse('c-1', { a: 1 })).toEqual({ v: 1, type: 'res', id: 'c-1', ok: true, data: { a: 1 } });
    expect(errorResponse('c-2', new TalkError(404, 'room not found'))).toEqual({
      v: 1,
      type: 'res',
      id: 'c-2',
      ok: false,
      error: { code: 404, reason: 'not_found', msg: 'room not found' },
    });
  });

  it('parseData throws 400 with field path', () => {
    const schema = z.object({ room_id: z.string() });
    expect(() => parseData(schema, {})).toThrow(TalkError);
    try {
      parseData(schema, {});
    } catch (e) {
      expect((e as TalkError).code).toBe(400);
      expect((e as TalkError).message).toMatch(/room_id/);
    }
  });
});
