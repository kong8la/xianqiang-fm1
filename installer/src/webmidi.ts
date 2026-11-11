import { handshake, type MidiIo } from "./session";
import type { Identity } from "./protocol";

const PORT_RE = /fm-1|felucca|sloop|dinghy|xian|ober|clipper|melodee|ota|m-vave|mvave/i;

export class WebMidiIo implements MidiIo {
  private queue: Uint8Array[] = [];
  private waiting: ((pkt: Uint8Array | null) => void)[] = [];
  private onmidimessage: (ev: MIDIMessageEvent) => void;

  constructor(
    private input: MIDIInput,
    private output: MIDIOutput,
  ) {
    this.onmidimessage = (ev) => {
      const data = ev.data;
      if (!data || data.length < 2 || data[0] !== 0xf0) return;
      const pkt = new Uint8Array(data);
      const w = this.waiting.shift();
      if (w) w(pkt);
      else this.queue.push(pkt);
    };
    this.input.addEventListener("midimessage", this.onmidimessage as EventListener);
  }

  async send(bytes: Uint8Array): Promise<void> {
    if (bytes[0] !== 0xf0 || bytes[bytes.length - 1] !== 0xf7) throw new Error("不是 SysEx");
    this.output.send(Array.from(bytes));
  }

  read(timeoutMs: number): Promise<Uint8Array | null> {
    if (this.queue.length) return Promise.resolve(this.queue.shift()!);
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
    this.input.removeEventListener("midimessage", this.onmidimessage as EventListener);
  }
}

export async function findFm1(access: MIDIAccess): Promise<{ io: WebMidiIo; identity: Identity; name: string } | null> {
  const outs: MIDIOutput[] = [];
  access.outputs.forEach((port) => outs.push(port));
  const inputs: MIDIInput[] = [];
  access.inputs.forEach((port) => inputs.push(port));
  for (const input of inputs) {
    if (!PORT_RE.test(input.name || "")) continue;
    const output = outs.find((o) => o.name === input.name) || outs.find((o) => PORT_RE.test(o.name || ""));
    if (!output) continue;
    const io = new WebMidiIo(input, output);
    const id = await handshake(io, 2, 700);
    if (id) return { io, identity: id, name: input.name || "FM-1" };
    io.close();
  }
  return null;
}
