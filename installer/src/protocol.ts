/** FM-1 USB-MIDI update framing. The device asks; the page only answers. */

export const HS_QUERY = Uint8Array.from([0xf0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7f, 0xf7]);
export const UPGRADE = Uint8Array.from([0xf0, 0x22, 0x24, 0x35, 0x7f, 0xf7]);
export const FINISH_CHECK = 0xe0000000;
export const FINISH_WRITE = 0xf0000000;
export const MAX_DATA = 512;
export const BLOCKS = 20;
export const BLK = 0x30;
export const KEEP = 0x2f;
export const MAX_BLOCKS = 64;
export const LOADER_MARK = "FELUCCA-LOADER-1";
/** Official V15 from m-vave/fm1_open @ b178ac4. */
export const V15_SHA256 = "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a";

export class PackageError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "PackageError";
  }
}

export function pack7(data: Uint8Array): Uint8Array {
  const out: number[] = [];
  let acc = 0;
  let nb = 0;
  for (const b of data) {
    acc |= b << nb;
    nb += 8;
    while (nb >= 7) {
      out.push(acc & 0x7f);
      acc >>>= 7;
      nb -= 7;
    }
  }
  if (nb) out.push(acc & 0x7f);
  return Uint8Array.from(out);
}

export function unpack7(data: Uint8Array): Uint8Array {
  const out: number[] = [];
  let acc = 0;
  let nb = 0;
  for (const b of data) {
    acc |= (b & 0x7f) << nb;
    nb += 7;
    while (nb >= 8) {
      out.push(acc & 0xff);
      acc >>>= 8;
      nb -= 8;
    }
  }
  return Uint8Array.from(out);
}

function checksum(bytes: Uint8Array): number {
  let s = 0;
  for (const b of bytes) s = (s + b) & 0xff;
  return (~s) & 0xff;
}

export function response(addr: number, data: Uint8Array, flags = 0): Uint8Array {
  const n = data.length;
  const body = new Uint8Array(14 + n + 1);
  body[0] = 0x00;
  body[1] = 0x59;
  body[2] = 0x30;
  body[3] = (n + 8) & 0xff;
  body[4] = ((n + 8) >> 8) & 0xff;
  body[5] = 0;
  body[6] = flags & 0xff;
  body[7] = addr & 0xff;
  body[8] = (addr >>> 8) & 0xff;
  body[9] = (addr >>> 16) & 0xff;
  body[10] = (addr >>> 24) & 0xff;
  body[11] = n & 0xff;
  body[12] = (n >> 8) & 0xff;
  body[13] = 0;
  body.set(data, 14);
  body[14 + n] = checksum(body.subarray(6, 14 + n));
  const packed = pack7(body);
  const pkt = new Uint8Array(packed.length + 2);
  pkt[0] = 0xf0;
  pkt.set(packed, 1);
  pkt[pkt.length - 1] = 0xf7;
  return pkt;
}

export interface ReadRequest {
  flags: number;
  addr: number;
  length: number;
}

export function parseRequest(pkt: Uint8Array): ReadRequest | null {
  if (pkt.length < 2 || pkt[0] !== 0xf0 || pkt[pkt.length - 1] !== 0xf7) return null;
  const u = unpack7(pkt.subarray(1, pkt.length - 1));
  if (u.length !== 15 || u[0] !== 0 || u[1] !== 0x59 || u[2] !== 0x30) return null;
  if (checksum(u.subarray(6, 14)) !== u[14]) return null;
  const addr = u[7] | (u[8] << 8) | (u[9] << 16) | (u[10] << 24);
  const length = u[11] | (u[12] << 8) | (u[13] << 16);
  return { flags: u[6], addr: addr >>> 0, length };
}

export interface Identity {
  text: string;
  model: string;
  version: number;
  loader: boolean;
}

export function parseIdentity(pkt: Uint8Array): Identity | null {
  if (pkt.length < 2 || pkt[0] !== 0xf0 || pkt[pkt.length - 1] !== 0xf7) return null;
  const d = unpack7(pkt.subarray(1, pkt.length - 1));
  if (d.length !== 34 || d[0] !== 0 || d[1] !== 0x59 || d[2] !== 0x11) return null;
  let text = "";
  for (let i = 6; i < 33 && d[i]; i++) text += String.fromCharCode(d[i]);
  const m = /^([^_]+)_(\d+)$/.exec(text);
  if (!m) return null;
  return { text, model: m[1], version: Number(m[2]), loader: m[1].toLowerCase().startsWith("ota-") };
}

export function makeIdentity(text: string): Uint8Array {
  const id = new Uint8Array(27);
  for (let i = 0; i < text.length && i < 27; i++) id[i] = text.charCodeAt(i);
  const body = new Uint8Array(6 + 27 + 1);
  body[0] = 0;
  body[1] = 0x59;
  body[2] = 0x11;
  body[3] = 27;
  body.set(id, 6);
  body[6 + 27] = checksum(id);
  const packed = pack7(body);
  const pkt = new Uint8Array(packed.length + 2);
  pkt[0] = 0xf0;
  pkt.set(packed, 1);
  pkt[pkt.length - 1] = 0xf7;
  return pkt;
}

export function makeRequest(addr: number, length: number, flags = 0): Uint8Array {
  const q = new Uint8Array(8);
  q[0] = flags & 0xff;
  q[1] = addr & 0xff;
  q[2] = (addr >>> 8) & 0xff;
  q[3] = (addr >>> 16) & 0xff;
  q[4] = (addr >>> 24) & 0xff;
  q[5] = length & 0xff;
  q[6] = (length >> 8) & 0xff;
  q[7] = (length >> 16) & 0xff;
  const body = new Uint8Array(6 + 8 + 1);
  body[0] = 0;
  body[1] = 0x59;
  body[2] = 0x30;
  body[3] = 8;
  body.set(q, 6);
  body[14] = checksum(q);
  const packed = pack7(body);
  const pkt = new Uint8Array(packed.length + 2);
  pkt[0] = 0xf0;
  pkt.set(packed, 1);
  pkt[pkt.length - 1] = 0xf7;
  return pkt;
}

function idChar(marker: number, index: number): boolean {
  const c = (marker - index - 1) & 0xff;
  return c >= 0x20 && c < 0x7f;
}

export function identityBlocks(raw: Uint8Array): number {
  const marker = (k: number) => ((k + 1) * BLK <= raw.length ? raw[k * BLK + KEEP] : null);
  let n = BLOCKS;
  while (n < MAX_BLOCKS) {
    const m = marker(n);
    if (m === null || m === 0x7d || !idChar(m, n)) break;
    n++;
  }
  return n > BLOCKS && marker(n) === 0x7d ? n : BLOCKS;
}

export function blockCount(raw: Uint8Array): number {
  let n = identityBlocks(raw);
  while (n < MAX_BLOCKS && (n + 1) * BLK <= raw.length && raw[n * BLK + KEEP] === 0x7d) n++;
  return n;
}

export function logicalImage(raw: Uint8Array, blocks: number): Uint8Array {
  const head = blocks * KEEP;
  const out = new Uint8Array(head + Math.max(0, raw.length - blocks * BLK));
  for (let i = 0; i < blocks; i++) out.set(raw.subarray(i * BLK, i * BLK + KEEP), i * KEEP);
  out.set(raw.subarray(blocks * BLK), head);
  return out;
}

export function productOf(raw: Uint8Array): string {
  if (raw.length < BLOCKS * BLK) throw new PackageError("文件太短，不是 FM-1 安装包");
  const n = identityBlocks(raw);
  let text = "";
  for (let i = 0; i < n; i++) {
    const m = raw[i * BLK + KEEP];
    if (m !== 0x7d) text += String.fromCharCode((m - i - 1) & 0xff);
  }
  return text;
}

function crc16(data: Uint8Array, crc = 0): number {
  for (const b of data) {
    crc ^= b << 8;
    for (let i = 0; i < 8; i++) crc = (crc & 0x8000 ? ((crc << 1) ^ 0x1021) : crc << 1) & 0xffff;
  }
  return crc & 0xffff;
}

function decEnc(bytes: Uint8Array): Uint8Array {
  const out = bytes.slice();
  let key = 0xffff;
  for (let i = 0; i < out.length; i++) {
    out[i] ^= key & 0xff;
    key = ((key << 1) ^ (key & 0x8000 ? 0x1021 : 0)) & 0xffff;
  }
  return out;
}

function u16(b: Uint8Array, o: number): number {
  return b[o] | (b[o + 1] << 8);
}
function u32(b: Uint8Array, o: number): number {
  return (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
}

/** What is wrong with the logical update image, or null when the CRCs agree. */
export function packageDamage(image: Uint8Array): string | null {
  if (image.length < 0x40) return "太短";
  const hdr = decEnc(image.subarray(0, 0x40));
  if (crc16(hdr.subarray(2, 0x40)) !== u16(hdr, 0)) return "文件头校验";
  const count = u16(hdr, 8);
  if (count < 1 || count > 8 || image.length < 0x40 + 0x50 * count) return "文件表";
  const list = image.subarray(0x40, 0x40 + 0x50 * count);
  if (crc16(list) !== u16(hdr, 2)) return "文件表校验";
  for (let i = 0; i < count; i++) {
    const e = decEnc(list.subarray(i * 0x50, (i + 1) * 0x50));
    const dcrc = u16(e, 4);
    const off = u32(e, 8);
    const size = u32(e, 12);
    let name = "";
    for (let k = 0x40; k < 0x50 && e[k]; k++) name += String.fromCharCode(e[k]);
    if (off + size > image.length) return `${name || "文件"}被截断`;
    const typ = u16(e, 0);
    if ((typ === 0 || typ === 100) && crc16(image.subarray(off, off + size)) !== dcrc) return `${name || "文件"}校验失败`;
  }
  return null;
}

export function deinterleave(raw: Uint8Array): { image: Uint8Array; blocks: number } {
  const n = blockCount(raw);
  const image = logicalImage(raw, n);
  if (n !== BLOCKS && packageDamage(image)) {
    const alt = logicalImage(raw, BLOCKS);
    if (!packageDamage(alt)) return { image: alt, blocks: BLOCKS };
  }
  return { image, blocks: n };
}

export function modelOf(identity: string): string {
  return identity.split("_")[0].replace(/^ota-/i, "");
}

const SMK = /^(SMK-37[ +].*|MKE-P37|STARRYKEY-37.*)$/i;

export function sameKeyboard(a: string, b: string): boolean {
  const ma = modelOf(a);
  const mb = modelOf(b);
  return ma === mb || (SMK.test(ma) && SMK.test(mb));
}

export async function sha256Hex(data: Uint8Array): Promise<string> {
  const copy = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength) as ArrayBuffer;
  const buf = await crypto.subtle.digest("SHA-256", copy);
  return [...new Uint8Array(buf)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

export function hasLoaderMark(raw: Uint8Array): boolean {
  const mark = new TextEncoder().encode(LOADER_MARK);
  outer: for (let i = 0; i + mark.length <= raw.length; i++) {
    for (let j = 0; j < mark.length; j++) if (raw[i + j] !== mark[j]) continue outer;
    return true;
  }
  return false;
}

export interface Prepared {
  image: Uint8Array;
  product: string;
  sha256: string;
  hasLoader: boolean;
}

/** Check a package before any MIDI traffic. Does not program flash. */
export async function preparePackage(raw: Uint8Array, expectedSha?: string): Promise<Prepared> {
  const sha256 = await sha256Hex(raw);
  if (expectedSha && sha256 !== expectedSha.toLowerCase()) {
    throw new PackageError("SHA-256 与目录不一致，已停止，没有写入。");
  }
  const product = productOf(raw);
  if (!/^[^_]+_\d+$/.test(product)) throw new PackageError(`身份「${product}」不是 FM-1 安装包。`);
  if (!product.startsWith("FM-1_")) throw new PackageError(`这个包是给 ${product} 的，本安装器只写 FM-1。`);
  const { image } = deinterleave(raw);
  const damage = packageDamage(image);
  if (damage) throw new PackageError(`安装包损坏（${damage}）。请重新下载。没有写入。`);
  const hasLoader = hasLoaderMark(raw);
  if (!hasLoader && sha256 !== V15_SHA256) {
    throw new PackageError("包里没有更新加载器标记。官方 V15 以外的这种文件会被拒绝，以免写入引导区。");
  }
  return { image, product, sha256, hasLoader };
}

export type ChipChoice = "unknown" | "856014" | "B36014";
export type FlashSupport = "new" | "old" | "any";

/** new = 认 0xB36014；old = 只认 Puya；any = 两者都可以。 */
/** Join a Vite base (`./` or `/xianqiang-fm1/`) with a catalogue file path. */
export function sitePath(base: string, file: string): string {
  const root = base.endsWith("/") ? base : `${base}/`;
  return root + file.replace(/^\//, "");
}

export function flashDecision(support: FlashSupport, chip: ChipChoice): "ok" | "refuse" | "warn" {
  if (support !== "old") return "ok";
  if (chip === "B36014") return "refuse";
  if (chip === "unknown") return "warn";
  return "ok";
}
