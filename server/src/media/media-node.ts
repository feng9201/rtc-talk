import * as mediasoup from 'mediasoup';
import type { types as ms } from 'mediasoup';
import { TalkError } from '../gateway/protocol.js';
import type { Logger } from '../logger.js';
import { MEDIA_CODECS } from './codecs.js';
import { RoomMedia, type AnswerHandler } from './room-media.js';
import { WorkerPool } from './worker-pool.js';

export interface MediaNodeOptions {
  numWorkers: number;
  workerBin: string | undefined;
  listenIp: string;
  announcedAddress: string | undefined;
  port: number;
  logger: Logger;
  onWorkerDied: (err: Error) => void;
}

function peerKey(roomId: string, uid: string): string {
  return `${roomId}\0${uid}`;
}

export class MediaNode {
  private readonly rooms = new Map<string, RoomMedia>();
  /** 同一 roomId 正在 createRouter 的共享 Promise，避免并发首建覆盖 */
  private readonly opening = new Map<string, Promise<RoomMedia | undefined>>();
  /** closeRoom 发生在 Router 创建完成之前 */
  private readonly discard = new Set<string>();
  /** removePeer 发生在 addPeer 的 await 返回之前，回来后不要再把这个人加进去 */
  private readonly skipped = new Set<string>();
  private shuttingDown = false;
  private readonly servers: ms.WebRtcServer[] = [];
  private answer: AnswerHandler = () => Promise.reject(new TalkError(500, 'no answer handler'));

  private constructor(
    private readonly pool: WorkerPool,
    private readonly log: Logger,
  ) {}

  static async create(opts: MediaNodeOptions): Promise<MediaNode> {
    const pool = await WorkerPool.create({
      numWorkers: opts.numWorkers,
      workerBin: opts.workerBin,
      logger: opts.logger,
      onWorkerDied: opts.onWorkerDied,
    });
    const node = new MediaNode(pool, opts.logger.child({ mod: 'media' }));
    const announced = opts.announcedAddress;
    for (const [i, worker] of pool.list().entries()) {
      const port = opts.port + i;
      const server = await worker.createWebRtcServer({
        listenInfos: [
          { protocol: 'udp', ip: opts.listenIp, port, ...(announced ? { announcedAddress: announced } : {}) },
          { protocol: 'tcp', ip: opts.listenIp, port, ...(announced ? { announcedAddress: announced } : {}) },
        ],
      });
      node.servers.push(server);
      node.log.info({ port, announced, pid: worker.pid }, 'webrtc server listening');
    }
    return node;
  }

  /** 由信令层注入：把 sfu.offer 发给客户端并拿到 answer SDP */
  setAnswerHandler(handler: AnswerHandler): void {
    this.answer = handler;
  }

  private serverFor(worker: ms.Worker): ms.WebRtcServer {
    const idx = this.pool.list().indexOf(worker);
    const server = this.servers[idx];
    if (!server) throw new Error('webrtc server missing for worker');
    return server;
  }

  async addPeer(roomId: string, uid: string, muted: boolean, videoOn = false): Promise<void> {
    const key = peerKey(roomId, uid);
    this.skipped.delete(key);
    const room = (await this.openRoom(roomId)) ?? this.rooms.get(roomId);
    if (this.skipped.has(key)) {
      this.skipped.delete(key);
      this.log.info({ room: roomId, uid }, 'addPeer skipped, peer already left');
      return;
    }
    if (!room) return;
    room.addPeer(uid, muted, videoOn);
  }

  /** 同一房间只创建一次 Router。并发的 addPeer 等待同一次创建。 */
  private openRoom(roomId: string): Promise<RoomMedia | undefined> {
    const existing = this.rooms.get(roomId);
    if (existing) return Promise.resolve(existing);
    const pending = this.opening.get(roomId);
    if (pending) return pending;
    const task = this.createRoom(roomId);
    this.opening.set(roomId, task);
    void task.finally(() => {
      if (this.opening.get(roomId) === task) this.opening.delete(roomId);
    });
    return task;
  }

  private async createRoom(roomId: string): Promise<RoomMedia | undefined> {
    const worker = this.pool.pick();
    const router = await worker.createRouter({ mediaCodecs: MEDIA_CODECS });
    const discarded = this.discard.delete(roomId);
    if (discarded || this.shuttingDown) {
      router.close();
      this.log.info({ room: roomId, router: router.id }, 'router discarded');
      return undefined;
    }
    const current = this.rooms.get(roomId);
    if (current) {
      router.close();
      this.log.warn({ room: roomId, router: router.id, kept: current.router.id }, 'duplicate router discarded');
      return current;
    }
    const room = new RoomMedia(roomId, router, this.serverFor(worker), this.log, this.answer);
    this.rooms.set(roomId, room);
    this.log.info({ room: roomId, router: router.id }, 'router created');
    return room;
  }

  removePeer(roomId: string, uid: string): void {
    const room = this.rooms.get(roomId);
    if (room?.peers.has(uid)) {
      room.removePeer(uid);
      return;
    }
    // 房间还在创建，或人还没写进 RoomMedia：记下，避免 await 回来后又加进去
    this.skipped.add(peerKey(roomId, uid));
    if (room || !this.opening.has(roomId)) {
      this.log.warn({ room: roomId, uid }, 'removePeer: peer not in media room');
    }
  }

  closeRoom(roomId: string): void {
    if (this.opening.has(roomId)) this.discard.add(roomId);
    const room = this.rooms.get(roomId);
    if (!room) return;
    const routerId = room.router.id;
    room.close();
    this.rooms.delete(roomId);
    this.log.info({ room: roomId, router: routerId }, 'router closed');
  }

  async publish(
    roomId: string,
    uid: string,
    sdp: string,
    tracks?: { mid: string; kind: 'audio' | 'video'; source?: 'mic' | 'camera' | 'screen'; closed?: boolean }[],
  ) {
    const room = this.rooms.get(roomId);
    const peer = room?.peers.get(uid);
    if (!room || !peer) throw new TalkError(409, 'not in room');
    const result = await peer.publish(sdp, tracks);
    for (const producer of peer.producers.values()) {
      for (const other of room.peers.values()) other.subscribe(producer, uid);
    }
    return result;
  }

  async setMuted(roomId: string, uid: string, muted: boolean, source: 'mic' | 'camera' = 'mic'): Promise<void> {
    const peer = this.rooms.get(roomId)?.peers.get(uid);
    if (!peer) {
      this.log.warn({ room: roomId, uid, source }, 'setMuted: peer not in media room');
      return;
    }
    await peer.setMuted(source, muted);
  }

  async restartIce(roomId: string, uid: string, pc: 'send' | 'recv'): Promise<void> {
    const peer = this.rooms.get(roomId)?.peers.get(uid);
    if (!peer) throw new TalkError(409, 'not in room');
    await peer.restartIce(pc);
  }

  onPeerOnline(roomId: string, uid: string): void {
    const peer = this.rooms.get(roomId)?.peers.get(uid);
    if (!peer?.needsFlush) return;
    peer.needsFlush = false;
    peer.flush();
  }

  status() {
    return { workers: this.pool.status(), rooms: this.rooms.size, version: mediasoup.version };
  }

  close(): void {
    this.shuttingDown = true;
    for (const roomId of this.opening.keys()) this.discard.add(roomId);
    for (const room of this.rooms.values()) room.close();
    this.rooms.clear();
    for (const s of this.servers) s.close();
    this.pool.close();
  }
}
