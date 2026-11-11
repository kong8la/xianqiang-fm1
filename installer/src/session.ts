import {
  FINISH_CHECK,
  FINISH_WRITE,
  HS_QUERY,
  MAX_DATA,
  UPGRADE,
  type Identity,
  parseIdentity,
  parseRequest,
  response,
  sameKeyboard,
} from "./protocol";

export interface MidiIo {
  send(bytes: Uint8Array): Promise<void>;
  read(timeoutMs: number): Promise<Uint8Array | null>;
  close(): void;
}

export class InstallError extends Error {
  constructor(
    readonly code: string,
    message: string,
  ) {
    super(message);
    this.name = "InstallError";
  }
}

export type Progress =
  | { phase: "start"; identity: string }
  | { phase: "check"; reads: number }
  | { phase: "loader" }
  | { phase: "write"; percent: number }
  | { phase: "reboot" }
  | { phase: "done"; identity: string };

const OWN_LOADER = /^ota-(FM-1|SMK-37 Pro|SMK-37 Elite|MKE-P37)_9\d\d$/i;

export async function handshake(io: MidiIo, tries = 3, waitMs = 800): Promise<Identity | null> {
  for (let t = 0; t < tries; t++) {
    await io.send(HS_QUERY);
    const end = Date.now() + waitMs;
    while (Date.now() < end) {
      const pkt = await io.read(Math.max(1, end - Date.now()));
      if (!pkt) break;
      const id = parseIdentity(pkt);
      if (id) return id;
    }
  }
  return null;
}

/**
 * Answer the device until it asks for `finish`. Read addresses are offsets into
 * the package image. Finish sentinels are not sliced out of that image, and a
 * request past the image is refused — nothing is sent.
 */
export async function serve(
  io: MidiIo,
  image: Uint8Array,
  finish: number,
  idleMs: number,
  onRead: (reads: number, end: number) => void,
): Promise<{ reads: number; finished: boolean }> {
  let reads = 0;
  let last = Date.now();
  for (;;) {
    const pkt = await io.read(50);
    if (!pkt) {
      if (Date.now() - last > idleMs) return { reads, finished: false };
      continue;
    }
    const req = parseRequest(pkt);
    if (!req) continue;
    last = Date.now();
    if (req.addr === FINISH_CHECK || req.addr === FINISH_WRITE) {
      const ok = new TextEncoder().encode("success\0");
      await io.send(response(req.addr, ok));
      if (req.addr === finish) return { reads, finished: true };
      continue;
    }
    if (req.length > MAX_DATA || req.addr + req.length > image.length) {
      throw new InstallError("badreq", `设备请求 ${toHex(req.addr)}+${req.length}，超出安装包。已停止，没有继续发送。`);
    }
    await io.send(response(req.addr, image.subarray(req.addr, req.addr + req.length), req.flags));
    reads++;
    onRead(reads, req.addr + req.length);
  }
}

function toHex(n: number): string {
  return "0x" + n.toString(16);
}

export interface SessionOptions {
  idleCheckMs?: number;
  idleWriteMs?: number;
  /** After step 1, open the update-loader port (it re-enumerates). */
  openLoader: () => Promise<MidiIo>;
  /** After the write, open the restarted firmware. */
  openRunning: () => Promise<MidiIo | null>;
}

export async function installPackage(
  dev: MidiIo,
  running: Identity,
  image: Uint8Array,
  product: string,
  opt: SessionOptions,
  onProgress: (p: Progress) => void,
): Promise<string> {
  if (!sameKeyboard(running.text, product)) {
    dev.close();
    throw new InstallError("model", `这台机器是 ${running.text}，安装包是 ${product}。型号不同，没有写入。`);
  }
  if (running.loader && !OWN_LOADER.test(running.text)) {
    dev.close();
    throw new InstallError(
      "foreign",
      `机器停在别的固件的更新模式（${running.text}）。请用那个固件自己的安装器写完，或先用 M-Flasher 恢复。`,
    );
  }
  let loader = dev;
  if (!running.loader) {
    onProgress({ phase: "start", identity: running.text });
    await dev.send(UPGRADE);
    const step1 = await serve(dev, image, FINISH_CHECK, opt.idleCheckMs ?? 8000, (reads) => onProgress({ phase: "check", reads }));
    dev.close();
    if (!step1.finished) {
      throw new InstallError("stopped", `核对阶段在 ${step1.reads} 次读取后中断。这一步还没有改写固件。请重新连接后再试。`);
    }
    onProgress({ phase: "loader" });
    loader = await opt.openLoader();
  }
  onProgress({ phase: "write", percent: 0 });
  await loader.send(UPGRADE);
  const step2 = await serve(loader, image, FINISH_WRITE, opt.idleWriteMs ?? 20000, (_reads, end) =>
    onProgress({ phase: "write", percent: Math.min(99, Math.floor((end * 100) / image.length)) }),
  );
  loader.close();
  if (!step2.finished) {
    throw new InstallError(
      "partial",
      `写入在 ${step2.reads} 次读取后中断。机器可能还停在更新模式：重新连接后再次安装同一个包，会接着写完。`,
    );
  }
  onProgress({ phase: "write", percent: 100 });
  onProgress({ phase: "reboot" });
  const back = await opt.openRunning();
  if (!back) throw new InstallError("noreturn", "FM-1 没有重新出现。请关机再开机；若屏幕停在更新模式，再次安装同一个包。");
  const id = await handshake(back, 4, 600);
  back.close();
  if (!id) throw new InstallError("noreturn", "机器重新连上了，但没有回答身份查询。");
  if (id.text !== product) {
    throw new InstallError("mismatch", `写完后机器报告 ${id.text}，目录里的包是 ${product}。请勿继续刷写，先备份并核对。`);
  }
  onProgress({ phase: "done", identity: id.text });
  return id.text;
}
