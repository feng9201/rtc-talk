import * as mediasoup from 'mediasoup';
import type { types as ms } from 'mediasoup';
import { TalkError } from '../gateway/protocol.js';
import type { Logger } from '../logger.js';
import { AUDIO_MEDIA_CODECS } from './codecs.js';
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

export class MediaNode {
  private readonly rooms = new Map<string, RoomMedia>();
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

  async addPeer(roomId: string, uid: string, muted: boolean): Promise<void> {
    let room = this.rooms.get(roomId);
    if (!room) {
      const worker = this.pool.pick();
      const router = await worker.createRouter({ mediaCodecs: AUDIO_MEDIA_CODECS });
      room = new RoomMedia(roomId, router, this.serverFor(worker), this.log, this.answer);
      this.rooms.set(roomId, room);
      this.log.info({ room: roomId, router: router.id }, 'router created');
    }
    room.addPeer(uid, muted);
  }

  removePeer(roomId: string, uid: string): void {
    this.rooms.get(roomId)?.removePeer(uid);
  }

  closeRoom(roomId: string): void {
    const room = this.rooms.get(roomId);
    if (!room) return;
    room.close();
    this.rooms.delete(roomId);
    this.log.info({ room: roomId }, 'router closed');
  }

  async publish(roomId: string, uid: string, sdp: string) {
    const room = this.rooms.get(roomId);
    const peer = room?.peers.get(uid);
    if (!room || !peer) throw new TalkError(409, 'not in room');
    const result = await peer.publish(sdp);
    const producer = peer.micProducer;
    if (producer) {
      for (const other of room.peers.values()) other.subscribe(producer, uid);
    }
    return result;
  }

  async setMuted(roomId: string, uid: string, muted: boolean): Promise<void> {
    await this.rooms.get(roomId)?.peers.get(uid)?.setMuted(muted);
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
    for (const room of this.rooms.values()) room.close();
    this.rooms.clear();
    for (const s of this.servers) s.close();
    this.pool.close();
  }
}
