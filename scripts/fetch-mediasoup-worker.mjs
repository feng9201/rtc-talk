#!/usr/bin/env node
// 下载与已安装 mediasoup 版本一致的官方预编译 worker（默认 kernel6 包），放到 .cache/mediasoup-worker/。
// mediasoup 按宿主内核主版本选包，开发机内核 5.15 没有对应的包；kernel6 包在 Ubuntu 22.04 上编译，
// 开发容器（共用宿主 5.15 内核）和测试机（6.8）都能运行，所以统一用它。
// 必须在 npm install 前设置 MEDIASOUP_WORKER_BIN，mediasoup 自身的 postinstall 才会跳过下载 / 源码编译。

import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync, chmodSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const DEST = join(ROOT, '.cache', 'mediasoup-worker');
const BIN = join(DEST, 'mediasoup-worker');
const VERSION_FILE = join(DEST, 'VERSION');

const KERNEL = process.env.MEDIASOUP_PREBUILT_KERNEL ?? '6';
const BASE_URL = process.env.MEDIASOUP_PREBUILT_BASE_URL ?? 'https://github.com/versatica/mediasoup/releases/download';

function log(msg) {
  console.log(`[fetch-mediasoup-worker] ${msg}`);
}

function mediasoupVersion() {
  for (const dir of [ROOT, join(ROOT, 'server')]) {
    const pkg = join(dir, 'node_modules', 'mediasoup', 'package.json');
    if (existsSync(pkg)) return JSON.parse(readFileSync(pkg, 'utf8')).version;
  }
  return null;
}

async function expectedDigest(version, asset) {
  const res = await fetch(`https://api.github.com/repos/versatica/mediasoup/releases/tags/${version}`, {
    headers: { Accept: 'application/vnd.github+json' },
  });
  if (!res.ok) throw new Error(`GitHub API ${res.status} ${res.statusText}`);
  const release = await res.json();
  const digest = release.assets?.find((a) => a.name === asset)?.digest;
  if (!digest?.startsWith('sha256:')) throw new Error(`release ${version} 中没有 ${asset} 的 sha256`);
  return digest.slice('sha256:'.length);
}

async function main() {
  if (process.platform !== 'linux') {
    log(`跳过：只处理 linux（当前 ${process.platform}）`);
    return;
  }
  if (!process.env.MEDIASOUP_WORKER_BIN) {
    log('警告：未设置 MEDIASOUP_WORKER_BIN，mediasoup 的 postinstall 可能已自行下载或编译 worker');
  }

  const version = mediasoupVersion();
  if (!version) {
    log('跳过：未安装 mediasoup');
    return;
  }
  if (existsSync(BIN) && existsSync(VERSION_FILE) && readFileSync(VERSION_FILE, 'utf8').trim() === version) {
    log(`已是 ${version}，无需下载`);
    return;
  }

  const arch = process.arch === 'arm64' ? 'arm64' : 'x64';
  const asset = `mediasoup-worker-${version}-linux-${arch}-kernel${KERNEL}.tgz`;
  const url = `${BASE_URL}/${version}/${asset}`;
  log(`下载 ${url}`);

  const res = await fetch(url);
  if (!res.ok) throw new Error(`下载失败：${res.status} ${res.statusText}`);
  const data = Buffer.from(await res.arrayBuffer());

  const actual = createHash('sha256').update(data).digest('hex');
  if (process.env.MEDIASOUP_PREBUILT_SKIP_DIGEST) {
    log(`跳过校验（sha256 ${actual}）`);
  } else {
    const expected = await expectedDigest(version, asset);
    if (actual !== expected) throw new Error(`sha256 不匹配：期望 ${expected}，实际 ${actual}`);
    log(`sha256 校验通过 ${actual}`);
  }

  rmSync(DEST, { recursive: true, force: true });
  mkdirSync(DEST, { recursive: true });
  const tgz = join(DEST, asset);
  writeFileSync(tgz, data);
  execFileSync('tar', ['-xzf', tgz, '-C', DEST]);
  rmSync(tgz);
  if (!existsSync(BIN)) throw new Error(`解压后没有找到 ${BIN}`);
  chmodSync(BIN, 0o755);
  writeFileSync(VERSION_FILE, `${version}\n`);
  log(`完成：${BIN}（${version}）`);
}

main().catch((err) => {
  console.error(`[fetch-mediasoup-worker] ${err.message}`);
  process.exit(1);
});
