import { FINISH_CHECK, FINISH_WRITE, HS_QUERY, UPGRADE, makeIdentity, makeRequest } from "./protocol";
import type { MidiIo } from "./session";

function same(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

/** An in-memory FM-1 that speaks the update protocol, for tests and the demo button. */
export class MockFm1 implements MidiIo {
  phase: "app" | "loader" | "done" = "app";
  /** Reads issued during the write step, so a test can see we never ask below a package offset. */
  writeAddrs: number[] = [];
  private queue: Uint8Array[] = [];
  private waiting: ((pkt: Uint8Array | null) => void)[] = [];
  private gap: Promise<void> = Promise.resolve();

  constructor(
    public running = "FM-1_941",
    public doneId = "FM-1_941",
    public loaderId = "ota-FM-1_941",
    private delayMs = 0,
  ) {}

  private push(pkt: Uint8Array) {
    const w = this.waiting.shift();
    if (w) w(pkt);
    else this.queue.push(pkt);
  }

  private async pause() {
    if (this.delayMs) await new Promise((r) => setTimeout(r, this.delayMs));
  }

  async send(bytes: Uint8Array): Promise<void> {
    if (same(bytes, HS_QUERY)) {
      const text = this.phase === "loader" ? this.loaderId : this.phase === "done" ? this.doneId : this.running;
      this.push(makeIdentity(text));
      return;
    }
    if (same(bytes, UPGRADE)) {
      if (this.phase === "app") {
        this.gap = this.driveCheck();
      } else if (this.phase === "loader") {
        this.gap = this.driveWrite();
      }
    }
  }

  async read(timeoutMs: number): Promise<Uint8Array | null> {
    if (this.queue.length) return this.queue.shift()!;
    return new Promise((resolve) => {
      const timer = setTimeout(() => {
        this.waiting = this.waiting.filter((w) => w !== onPkt);
        resolve(null);
      }, timeoutMs);
      const onPkt = (pkt: Uint8Array | null) => {
        clearTimeout(timer);
        resolve(pkt);
      };
      this.waiting.push(onPkt);
    });
  }

  close(): void {
    /* the page keeps the mock across the re-enumeration */
  }

  private async driveCheck() {
    await this.pause();
    this.push(makeRequest(0, 64));
    await this.pause();
    this.push(makeRequest(0x120, 32));
    await this.pause();
    this.push(makeRequest(FINISH_CHECK, 0));
    this.phase = "loader";
  }

  private async driveWrite() {
    const plan = [0x40, 0x400, 0x2000, 0x10000];
    for (const addr of plan) {
      await this.pause();
      this.writeAddrs.push(addr);
      this.push(makeRequest(addr, 48));
    }
    await this.pause();
    this.push(makeRequest(FINISH_WRITE, 0));
    this.phase = "done";
  }

  /** Resolves after the scripted side has finished queueing the current step. */
  async settled(): Promise<void> {
    await this.gap;
  }
}
