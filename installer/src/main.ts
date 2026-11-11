import { MockFm1 } from "./mock";
import { flashDecision, preparePackage, sitePath, type ChipChoice, type FlashSupport, type Identity } from "./protocol";
import { handshake, installPackage, type MidiIo, type Progress } from "./session";
import { findFm1 } from "./webmidi";

interface Entry {
  id: string;
  kind: "install" | "official";
  featured?: boolean;
  name: string;
  author: string;
  summary: string;
  license: string;
  version?: string;
  source?: string | null;
  flash?: FlashSupport;
  sha256?: string;
  size?: number;
  file?: string;
  product?: string | null;
}

interface Catalogue {
  flashNote: string;
  attribution: string;
  firmwares: Entry[];
  official: Entry;
}

type Conn =
  | { mode: "none" }
  | { mode: "live"; io: MidiIo; identity: Identity; port: string }
  | { mode: "mock"; io: MockFm1; identity: Identity };

const state: {
  cat: Catalogue | null;
  error: string;
  chip: ChipChoice;
  conn: Conn;
  busy: boolean;
  dialog: null | { title: string; body: string; percent: number; error?: string; done?: boolean };
  warn: null | { entry: Entry; ack: boolean };
} = {
  cat: null,
  error: "",
  chip: (localStorage.getItem("xq-chip") as ChipChoice) || "unknown",
  conn: { mode: "none" },
  busy: false,
  dialog: null,
  warn: null,
};

const app = document.querySelector<HTMLElement>("#app")!;
app.addEventListener("click", (ev) => {
  const t = (ev.target as HTMLElement).closest<HTMLElement>("[data-act]");
  if (!t) return;
  const act = t.dataset.act;
  if (act === "connect") void connectLive();
  if (act === "mock") void connectMock();
  if (act === "install") void beginInstall(t.dataset.id || "");
  if (act === "v15") void beginInstall("v15");
  if (act === "close") {
    state.dialog = null;
    state.warn = null;
    render();
  }
  if (act === "ack-install") {
    if (state.warn) {
      const entry = state.warn.entry;
      state.warn = null;
      void runInstall(entry, true);
    }
  }
});
app.addEventListener("change", (ev) => {
  const t = ev.target as HTMLSelectElement;
  if (t.id === "chip") {
    state.chip = t.value as ChipChoice;
    localStorage.setItem("xq-chip", state.chip);
    render();
  }
});

function esc(s: string): string {
  return s.replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]!);
}

function flashTag(f?: FlashSupport): string {
  if (f === "new" || f === "any") return `<span class="tag good">支持 0xB36014</span>`;
  if (f === "old") return `<span class="tag bad">仅早期闪存</span>`;
  return "";
}

function identityText(): string {
  if (state.conn.mode === "none") return "还没有连接 FM-1";
  const id = state.conn.identity;
  const where = id.loader ? "停在更新模式，再次安装会接着写" : "正在运行";
  const how = state.conn.mode === "mock" ? "模拟机器" : state.conn.port;
  return `${id.text} · ${where} · ${how}`;
}

function firmwares(): Entry[] {
  return state.cat?.firmwares ?? [];
}

function restoreLabel(): string {
  const official = state.cat?.official;
  if (!official) return "恢复官方 V15";
  return flashDecision(official.flash || "old", state.chip) === "refuse" ? "新闪存不可装" : "恢复官方 V15";
}

function render() {
  const cat = state.cat;
  const cards = firmwares()
    .map((e) => {
      const decision = e.flash ? flashDecision(e.flash, state.chip) : "ok";
      const blocked = decision === "refuse";
      return `<article class="card ${e.featured ? "featured" : ""}">
        <h2>${esc(e.name)}</h2>
        <p class="who">${esc(e.author)} · ${esc(e.license)}${e.version ? ` · ${esc(e.version)}` : ""}</p>
        <div class="tags">${e.featured ? `<span class="tag">本仓库</span>` : ""}${flashTag(e.flash)}</div>
        <p>${esc(e.summary)}</p>
        <div class="row">
          ${e.source ? `<a href="${esc(e.source)}" target="_blank" rel="noreferrer">源码</a>` : `<span class="who">源码在本仓库</span>`}
          <button data-act="install" data-id="${esc(e.id)}" ${state.busy ? "disabled" : ""}>${blocked ? "新闪存不可装" : "安装"}</button>
        </div>
      </article>`;
    })
    .join("");

  app.innerHTML = `
  <div class="wrap">
    <header class="top">
      <div>
        <p class="mark">FM-1 · Web MIDI</p>
        <h1>道哥合成器安装器</h1>
        <p class="lede">在 Chrome 或 Edge 里给 FM-1 安装道哥合成器。机器发读取请求，这个页面只回答核对过的字节，不直接写引导区。</p>
      </div>
      <div class="actions">
        <button data-act="connect">连接 FM-1</button>
        <button class="ghost" data-act="mock">演示 0xB36014</button>
      </div>
    </header>
    <section class="device">
      <div>
        <strong>${esc(identityText())}</strong>
        <p class="meta">${state.conn.mode === "none" ? "需要 USB 数据线，系统里会出现一个 MIDI 设备。Safari 和手机浏览器没有 Web MIDI。" : "身份来自更新协议的握手。闪存型号协议不报告，请按购买时间自己选。"}</p>
      </div>
      <label class="chip-pick">这台机器的闪存
        <select id="chip">
          <option value="unknown" ${state.chip === "unknown" ? "selected" : ""}>还不确定</option>
          <option value="856014" ${state.chip === "856014" ? "selected" : ""}>早期 Puya · 0x856014</option>
          <option value="B36014" ${state.chip === "B36014" ? "selected" : ""}>较新 UC25HQ80 · 0xB36014</option>
        </select>
      </label>
    </section>
    <section class="banner">
      <strong>先备份整片 1 MiB 闪存。</strong>
      单区闪存，没有第二份固件可回退。用
      <a href="https://github.com/jvitkauskas/m-flasher" target="_blank" rel="noreferrer">M-Flasher</a>
      的 DOWNLOAD MODE（SysEx <code>F0 22 24 35 7D F7</code>）把整片读出来，或用
      <a href="https://github.com/Quixotic7/MvaveFM1Unbricker" target="_blank" rel="noreferrer">MvaveFM1Unbricker</a>。
      电脑上出现名为 WL82 UBOOT1.00 的磁盘时不要格式化。
    </section>
    ${state.error ? `<p class="err">${esc(state.error)}</p>` : ""}
    ${cat ? `<div class="grid">${cards}</div>` : `<p>正在读取目录…</p>`}
    <section class="rescue" id="rescue">
      <h2>救砖</h2>
      <div class="restore">
        <h3>恢复官方固件</h3>
        <p>刷坏之后可以写回 M-VAVE 官方 V15（FM-1_015）。这不是另一台乐器，只用来把机器救回原厂。它只认早期 Puya（0x856014）。选了较新的 UC25HQ80 之后，这里会拒绝写入。</p>
        <button class="ghost" data-act="v15" ${state.busy ? "disabled" : ""}>${restoreLabel()}</button>
      </div>
      <ol>
        <li>固件还能起来时，开机按住 <strong>OCT−</strong>。道哥合成器会进入 USB 救援，安装器仍可写。</li>
        <li>屏幕停住、电脑里是 <strong>WL82 UBOOT1.00</strong>：这是芯片 ROM 的下载模式。用 M-Flasher 备份或写回，不要初始化那只磁盘。</li>
        <li>USB 上什么都没有：用 RP2040（Seeed XIAO 或 Pico）做 USB_KEY，只接 D+、D−、GND，不接 VBUS。ROM 认的是字 <strong>0x16EF</strong>。做好的线见 <a href="https://github.com/kurogedelic/FM-1-transporter" target="_blank" rel="noreferrer">FM-1 Transporter</a>。</li>
        <li>写回时只动应用区 <code>0x4000</code>–<code>0x93000</code>。引导区 <code>0x0000</code>–<code>0x3FFF</code> 留给原厂引导。UC25HQ80（<code>0xB36014</code>）不要装 m-bootloader。</li>
      </ol>
    </section>
    <footer>${esc(cat?.attribution || "")} ${esc(cat?.flashNote || "")}</footer>
  </div>
  ${dialogHtml()}`;
}

function dialogHtml(): string {
  if (state.warn) {
    const e = state.warn.entry;
    return `<div class="dialog-back"><div class="dialog" role="dialog">
      <h2>这台机器的闪存还不确定</h2>
      <p>${esc(e.name)} 只认早期 Puya（0x856014）。若实际是 UC25HQ80（0xB36014），设置不会存进闪存，以后也可能无法再更新。</p>
      <p>协议握手只报告固件身份，不报告闪存型号。请先在上方选择闪存。仍要安装的话，确认你知道这台不是 0xB36014。</p>
      <div class="actions">
        <button class="ghost" data-act="close">取消</button>
        <button class="danger" data-act="ack-install">我确认不是新闪存，继续</button>
      </div>
    </div></div>`;
  }
  if (!state.dialog) return "";
  const d = state.dialog;
  return `<div class="dialog-back"><div class="dialog" role="dialog">
    <h2>${esc(d.title)}</h2>
    <div class="bar"><span style="width:${d.percent}%"></span></div>
    <p class="${d.error ? "err" : d.done ? "ok" : ""}">${esc(d.body)}</p>
    ${d.done || d.error ? `<button data-act="close">关闭</button>` : ""}
  </div></div>`;
}

async function connectLive() {
  state.error = "";
  const midi = navigator.requestMIDIAccess;
  if (!midi) {
    state.error = "这个浏览器没有 Web MIDI。请用电脑上的 Chrome 或 Edge。";
    render();
    return;
  }
  try {
    const access = await navigator.requestMIDIAccess({ sysex: true });
    const found = await findFm1(access);
    if (!found) {
      state.error = "没有 FM-1 回答身份查询。换一根能传数据的线，并关掉占用 MIDI 的其他程序。";
      state.conn = { mode: "none" };
    } else {
      state.conn = { mode: "live", io: found.io, identity: found.identity, port: found.name };
    }
  } catch {
    state.error = "浏览器拒绝了 SysEx。请在提示里允许，并使用 Chrome 或 Edge。";
  }
  render();
}

async function connectMock() {
  const mock = new MockFm1("FM-1_941", "FM-1_941", "ota-FM-1_941", 120);
  const id = await handshake(mock, 1, 400);
  if (!id) {
    state.error = "模拟机器没有回答。";
    render();
    return;
  }
  state.error = "";
  state.conn = { mode: "mock", io: mock, identity: id };
  if (state.chip === "unknown") state.chip = "B36014";
  render();
}

function entryById(id: string): Entry | null {
  const cat = state.cat;
  if (!cat) return null;
  if (id === "v15") return { ...cat.official, kind: "official", flash: cat.official.flash || "old" };
  return cat.firmwares.find((e) => e.id === id) || null;
}

async function beginInstall(id: string) {
  const entry = entryById(id);
  if (!entry || !entry.file || !entry.sha256) return;
  if (state.conn.mode === "none") {
    state.error = "先连接 FM-1，或打开演示机器。";
    render();
    return;
  }
  const decision = flashDecision(entry.flash || "old", state.chip);
  if (decision === "refuse") {
    state.dialog = {
      title: "已拒绝安装",
      body: entry.id === "v15"
        ? "官方 V15 不能装到闪存 0xB36014 上：它只认早期 Puya。这颗闪存请安装道哥合成器。没有写入。"
        : `${entry.name} 不能装到闪存 0xB36014 上：它只认早期 Puya，设置会丢，更新也可能被拒绝。没有写入。`,
      percent: 0,
      error: "refused",
    };
    render();
    return;
  }
  if (decision === "warn") {
    state.warn = { entry, ack: false };
    render();
    return;
  }
  await runInstall(entry, false);
}

async function runInstall(entry: Entry, acknowledged: boolean) {
  if (!entry.file || !entry.sha256) return;
  if (state.conn.mode === "none") return;
  state.busy = true;
  state.dialog = { title: `安装 ${entry.name}`, body: "正在下载并核对 SHA-256…", percent: 4 };
  render();
  try {
    const res = await fetch(sitePath(import.meta.env.BASE_URL, entry.file));
    if (!res.ok) throw new Error(`下载失败（${res.status}）`);
    const raw = new Uint8Array(await res.arrayBuffer());
    const prepared = await preparePackage(raw, entry.sha256);
    if (entry.product && prepared.product !== entry.product) {
      throw new Error(`包身份是 ${prepared.product}，目录记录的是 ${entry.product}。`);
    }
    const io = state.conn.io;
    if (state.conn.mode === "mock") state.conn.io.doneId = prepared.product;
    const running = state.conn.identity;
    state.dialog = { title: `安装 ${entry.name}`, body: acknowledged ? "已确认闪存不是 0xB36014。正在开始…" : "核对通过。正在开始…", percent: 8 };
    render();
    const reopen = async () => io;
    await installPackage(io, running, prepared.image, prepared.product, {
      idleCheckMs: 4000,
      idleWriteMs: 8000,
      openLoader: reopen,
      openRunning: reopen,
    }, (p: Progress) => {
      state.dialog = progressDialog(entry.name, p);
      render();
    });
    if (state.conn.mode === "mock") {
      state.conn.identity = { text: prepared.product, model: "FM-1", version: 0, loader: false };
    }
  } catch (err) {
    state.dialog = {
      title: "安装没有完成",
      body: err instanceof Error ? err.message : String(err),
      percent: state.dialog?.percent || 0,
      error: "failed",
    };
  } finally {
    state.busy = false;
    render();
  }
}

function progressDialog(name: string, p: Progress): NonNullable<typeof state.dialog> {
  if (p.phase === "start") return { title: `安装 ${name}`, body: `正在让 ${p.identity} 进入更新模式…`, percent: 12 };
  if (p.phase === "check") return { title: `安装 ${name}`, body: `正在核对安装包（${p.reads} 次读取）。这一步还不改写固件。`, percent: 18 + Math.min(12, p.reads) };
  if (p.phase === "loader") return { title: `安装 ${name}`, body: "已切到更新加载器。", percent: 32 };
  if (p.phase === "write") return { title: `安装 ${name}`, body: p.percent >= 100 ? "写入结束，正在等待重启。" : "正在写入。不要拔线。", percent: 32 + Math.floor(p.percent * 0.6) };
  if (p.phase === "reboot") return { title: `安装 ${name}`, body: "正在等 FM-1 重新出现…", percent: 94 };
  return { title: "安装完成", body: `FM-1 现在运行 ${p.identity}。可以关机再开机确认。`, percent: 100, done: true };
}

async function main() {
  render();
  try {
    const res = await fetch(sitePath(import.meta.env.BASE_URL, "catalogue.json"));
    if (!res.ok) throw new Error(String(res.status));
    state.cat = (await res.json()) as Catalogue;
  } catch {
    state.error = "读不到 catalogue.json。请先运行 npm run catalogue。";
  }
  render();
}

void main();
