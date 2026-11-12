import { readdirSync, readFileSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { MockFm1 } from "../src/mock";
import {
  FINISH_CHECK,
  flashDecision,
  makeRequest,
  pack7,
  packageDamage,
  parseIdentity,
  parseRequest,
  preparePackage,
  productOf,
  sitePath,
  response,
  sha256Hex,
  unpack7,
  V15_SHA256,
} from "../src/protocol";
import { handshake, installPackage } from "../src/session";

const fw = new Uint8Array(readFileSync(new URL("../../dist/xianqiang-0.1.fwsc", import.meta.url)));

describe("framing", () => {
  it("round-trips 7-bit packing", () => {
    const samples = [Uint8Array.of(0, 1, 0x80, 0xff, 0x59), fw.subarray(0, 300)];
    for (const s of samples) {
      const back = unpack7(pack7(s));
      expect(Array.from(back)).toEqual(Array.from(s));
    }
  });

  it("matches the Python packer on a mixed buffer", () => {
    const sample = Uint8Array.from([0x00, 0x59, 0x30, 0x10, 0x80, 0xff, 0x7f, 0x01]);
    const py = execFileSync("python3", ["-c", `
import importlib.util
spec = importlib.util.spec_from_file_location("fm1", "tools/fm1_install.py")
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
print(m.pack7(bytes([0x00,0x59,0x30,0x10,0x80,0xff,0x7f,0x01])).hex())
print(m.product_of(open("dist/xianqiang-0.1.fwsc","rb").read()))
`], { cwd: fileURLToPath(new URL("../..", import.meta.url)) }).toString().trim().split("\n");
    expect(Buffer.from(pack7(sample)).toString("hex")).toBe(py[0]);
    expect(productOf(fw)).toBe(py[1]);
  });

  it("parses a request the device would send", () => {
    const pkt = makeRequest(0x120, 32, 0);
    const req = parseRequest(pkt);
    expect(req).toEqual({ flags: 0, addr: 0x120, length: 32 });
  });

  it("rejects a request with a bad checksum", () => {
    const pkt = makeRequest(0, 8);
    pkt[pkt.length - 2] ^= 0x1;
    expect(parseRequest(pkt)).toBeNull();
  });

  it("builds an identity frame", () => {
    expect(parseIdentity(response(0, new Uint8Array(4)))).toBeNull();
  });
});

describe("package", () => {
  it("accepts the 弦腔 release and names FM-1_941", async () => {
    const sha = await sha256Hex(fw);
    const prepared = await preparePackage(fw, sha);
    expect(prepared.product).toBe("FM-1_941");
    expect(prepared.hasLoader).toBe(true);
    expect(packageDamage(prepared.image)).toBeNull();
  });

  it("refuses a package whose hash does not match", async () => {
    await expect(preparePackage(fw, "00".repeat(32))).rejects.toThrow(/SHA-256/);
  });

  it("refuses a truncated file", async () => {
    await expect(preparePackage(fw.subarray(0, 100))).rejects.toThrow(/太短|损坏/);
  });

  it("pins the official V15 hash", () => {
    expect(V15_SHA256).toHaveLength(64);
  });
});

describe("pages base path", () => {
  it("keeps catalogue and package URLs under the project site", () => {
    expect(sitePath("/xianqiang-fm1/", "catalogue.json")).toBe("/xianqiang-fm1/catalogue.json");
    expect(sitePath("/xianqiang-fm1/", "pkg/xianqiang-0.1.fwsc")).toBe("/xianqiang-fm1/pkg/xianqiang-0.1.fwsc");
    expect(sitePath("./", "pkg/FM-1_015.fwsc")).toBe("./pkg/FM-1_015.fwsc");
  });
});

describe("catalogue", () => {
  it("offers only XianQiang, with official V15 kept for restore", () => {
    const doc = JSON.parse(readFileSync(new URL("../public/catalogue.json", import.meta.url), "utf8"));
    expect(doc.firmwares.map((e: { id: string }) => e.id)).toEqual(["daoge", "daoge-02", "xianqiang"]);
    expect(doc.firmwares[0].file).toBe("pkg/daoge-0.2.1.fwsc");
    expect(doc.firmwares[0].product).toBe("FM-1_942");
    expect(doc.firmwares[0].name).toBe("道哥合成器");
    expect(doc.firmwares[1].file).toBe("pkg/daoge-0.2.fwsc");
    expect(doc.firmwares[1].name).toBe("道哥合成器 0.2");
    expect(doc.firmwares[2].file).toBe("pkg/xianqiang-0.1.fwsc");
    expect(doc.firmwares[2].name).toBe("道哥合成器 0.1");
    expect(doc.firmwares[0].flash).toBe("new");
    expect(doc.official.id).toBe("v15");
    expect(doc.official.file).toBe("pkg/FM-1_015.fwsc");
    expect(doc.official.flash).toBe("old");
    expect(doc.official.sha256).toBe(V15_SHA256);
    expect(doc.links).toBeUndefined();
    const names = readdirSync(new URL("../public/pkg/", import.meta.url)).sort();
    expect(names).toEqual(["FM-1_015.fwsc", "daoge-0.2.1.fwsc", "daoge-0.2.fwsc", "xianqiang-0.1.fwsc"]);
  });
});

describe("flash policy", () => {
  it("refuses an old-only firmware on 0xB36014", () => {
    expect(flashDecision("old", "B36014")).toBe("refuse");
    expect(flashDecision("new", "B36014")).toBe("ok");
    expect(flashDecision("any", "B36014")).toBe("ok");
  });

  it("warns when the chip is unknown", () => {
    expect(flashDecision("old", "unknown")).toBe("warn");
    expect(flashDecision("old", "856014")).toBe("ok");
  });
});

describe("simulated install", () => {
  it("runs check, loader, write and reboot against a mock FM-1", async () => {
    const prepared = await preparePackage(fw);
    const mock = new MockFm1("FM-1_015", prepared.product, "ota-FM-1_941", 0);
    const running = await handshake(mock, 1, 200);
    expect(running?.text).toBe("FM-1_015");
    const phases: string[] = [];
    const text = await installPackage(mock, running!, prepared.image, prepared.product, {
      idleCheckMs: 1500,
      idleWriteMs: 1500,
      openLoader: async () => mock,
      openRunning: async () => mock,
    }, (p) => phases.push(p.phase));
    expect(text).toBe("FM-1_941");
    expect(phases[0]).toBe("start");
    expect(phases).toContain("loader");
    expect(phases.at(-1)).toBe("done");
    expect(mock.writeAddrs.length).toBeGreaterThan(0);
    expect(Math.min(...mock.writeAddrs)).toBeGreaterThanOrEqual(0);
  });

  it("does not answer a read past the image", async () => {
    const prepared = await preparePackage(fw);
    const io = {
      sent: [] as Uint8Array[],
      async send(b: Uint8Array) { this.sent.push(b); },
      async read(): Promise<Uint8Array | null> {
        if (this.sent.length === 0) return null;
        return makeRequest(prepared.image.length + 10, 16);
      },
      close() {},
    };
    // prime: first read returns the bad request
    let once = true;
    io.read = async () => {
      if (!once) return null;
      once = false;
      return makeRequest(prepared.image.length + 10, 16);
    };
    const { serve } = await import("../src/session");
    await expect(serve(io, prepared.image, FINISH_CHECK, 200, () => {})).rejects.toThrow(/超出安装包/);
    expect(io.sent).toHaveLength(0);
  });
});
