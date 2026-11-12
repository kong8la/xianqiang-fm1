#!/usr/bin/env python3
"""Build installer/public/catalogue.json for 道哥合成器 and the official V15 rescue package.

The page offers only this repository's firmware. Official M-VAVE V15
(FM-1_015 from m-vave/fm1_open @ b178ac4) stays so a bad flash can be written
back to the factory image. It is not another instrument. SHA-256 is computed
from the bytes stored next to the page.
"""
from __future__ import annotations

import hashlib
import json
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public"
PKG = PUBLIC / "pkg"
UA = {"User-Agent": "xianqiang-installer", "Accept": "application/vnd.github+json"}

V15_URL = (
    "https://raw.githubusercontent.com/m-vave/fm1_open/"
    "b178ac43c25f010c60a878056fb5331fe693fd62/sdk/firmware/FM-1_015.fwsc"
)
V15_SHA = "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a"
KEEP = {"daoge-0.2.1.fwsc", "daoge-0.2.fwsc", "xianqiang-0.1.fwsc", "FM-1_015.fwsc"}


def get(url: str) -> bytes:
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=90) as res:
        return res.read()


def sha256(blob: bytes) -> str:
    return hashlib.sha256(blob).hexdigest()


def main() -> None:
    PKG.mkdir(parents=True, exist_ok=True)

    def mirror(name: str) -> bytes:
        src = ROOT.parents[0] / "dist" / name
        dst = PKG / name
        if src.is_file():
            dst.write_bytes(src.read_bytes())
        return dst.read_bytes()

    cur = mirror("daoge-0.2.1.fwsc")
    mid = mirror("daoge-0.2.fwsc")
    prev = mirror("xianqiang-0.1.fwsc")

    v15_path = PKG / "FM-1_015.fwsc"
    if v15_path.is_file() and sha256(v15_path.read_bytes()) == V15_SHA:
        v15 = v15_path.read_bytes()
    else:
        v15 = get(V15_URL)
        digest = sha256(v15)
        if digest != V15_SHA:
            raise SystemExit(f"V15 hash {digest} != {V15_SHA}")
        v15_path.write_bytes(v15)

    for path in PKG.glob("*.fwsc"):
        if path.name not in KEEP:
            path.unlink()
            print(f"removed {path.name}")

    cur_sha = sha256(cur)
    mid_sha = sha256(mid)
    prev_sha = sha256(prev)
    v15_sha = sha256(v15)
    doc = {
        "generatedBy": "installer/scripts/fetch-catalogue.py",
        "flashNote": "道哥合成器认早期 Puya（0x856014）和较新的 UC25HQ80（0xB36014）。官方 V15 只用来在救砖时写回原厂，只认早期 Puya。",
        "attribution": "官方 V15 来自 m-vave/fm1_open 提交 b178ac4 的 sdk/firmware/FM-1_015.fwsc。",
        "firmwares": [
            {
                "id": "daoge",
                "kind": "install",
                "featured": True,
                "name": "道哥合成器",
                "author": "本仓库",
                "summary": "0.2.1：丝印按键都会在屏幕上打开对应的页。认 Puya 与 UC25HQ80。",
                "license": "GPL-3.0-only",
                "version": "0.2.1",
                "source": "https://github.com/kong8la/xianqiang-fm1",
                "flash": "new",
                "product": "FM-1_942",
                "sha256": cur_sha,
                "size": len(cur),
                "file": "pkg/daoge-0.2.1.fwsc",
                "tag": "v0.2.1",
            },
            {
                "id": "daoge-02",
                "kind": "install",
                "featured": False,
                "name": "道哥合成器 0.2",
                "author": "本仓库",
                "summary": "0.2：更多预设和用户槽、混响延时合唱、琶音和音序、筝锣风琴管。",
                "license": "GPL-3.0-only",
                "version": "0.2",
                "source": "https://github.com/kong8la/xianqiang-fm1",
                "flash": "new",
                "product": "FM-1_942",
                "sha256": mid_sha,
                "size": len(mid),
                "file": "pkg/daoge-0.2.fwsc",
                "tag": "v0.2",
            },
            {
                "id": "xianqiang",
                "kind": "install",
                "featured": False,
                "name": "道哥合成器 0.1",
                "author": "本仓库",
                "summary": "上一版。六复音物理建模：拨弦、木棒、钟、鼓膜、拉弦和单簧管/长笛。",
                "license": "GPL-3.0-only",
                "version": "0.1",
                "source": "https://github.com/kong8la/xianqiang-fm1",
                "flash": "new",
                "product": "FM-1_941",
                "sha256": prev_sha,
                "size": len(prev),
                "file": "pkg/xianqiang-0.1.fwsc",
                "tag": "v0.1",
            },
        ],
        "official": {
            "id": "v15",
            "kind": "official",
            "name": "官方 V15",
            "author": "M-VAVE",
            "summary": "救砖时写回 M-VAVE 的 FM-1_015。不是另一台乐器。只认早期 Puya。写入前按 SHA-256 核对。",
            "license": "Apache-2.0",
            "version": "015",
            "source": "https://github.com/m-vave/fm1_open",
            "flash": "old",
            "product": "FM-1_015",
            "sha256": v15_sha,
            "size": len(v15),
            "file": "pkg/FM-1_015.fwsc",
            "tag": "b178ac4",
        },
    }
    (PUBLIC / "catalogue.json").write_text(json.dumps(doc, ensure_ascii=False, indent=2) + "\n")
    print(f"catalogue 0.2.1 {cur_sha[:12]} + 0.2 {mid_sha[:12]} + 0.1 {prev_sha[:12]} + V15 {v15_sha[:12]}")


if __name__ == "__main__":
    main()
