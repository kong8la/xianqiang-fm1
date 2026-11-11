#!/usr/bin/env python3
"""Build installer/public/catalogue.json for 弦腔 and the official V15 rescue package.

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
KEEP = {"xianqiang-0.1.fwsc", "FM-1_015.fwsc"}


def get(url: str) -> bytes:
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=90) as res:
        return res.read()


def sha256(blob: bytes) -> str:
    return hashlib.sha256(blob).hexdigest()


def main() -> None:
    PKG.mkdir(parents=True, exist_ok=True)

    xq_src = ROOT.parents[0] / "dist" / "xianqiang-0.1.fwsc"
    xq_dst = PKG / "xianqiang-0.1.fwsc"
    if xq_src.is_file():
        xq_dst.write_bytes(xq_src.read_bytes())
    xq = xq_dst.read_bytes()

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

    xq_sha = sha256(xq)
    v15_sha = sha256(v15)
    doc = {
        "generatedBy": "installer/scripts/fetch-catalogue.py",
        "flashNote": "弦腔认早期 Puya（0x856014）和较新的 UC25HQ80（0xB36014）。官方 V15 只用来在救砖时写回原厂，只认早期 Puya。",
        "attribution": "官方 V15 来自 m-vave/fm1_open 提交 b178ac4 的 sdk/firmware/FM-1_015.fwsc。",
        "firmwares": [
            {
                "id": "xianqiang",
                "kind": "install",
                "featured": True,
                "name": "弦腔 XianQiang",
                "author": "本仓库",
                "summary": "六复音物理建模：拨弦、木棒、钟、鼓膜、拉弦和单簧管/长笛。认 Puya 与 UC25HQ80。",
                "license": "GPL-3.0-only",
                "version": "0.1",
                "source": "https://github.com/kong8la/xianqiang-fm1",
                "flash": "new",
                "product": "FM-1_941",
                "sha256": xq_sha,
                "size": len(xq),
                "file": "pkg/xianqiang-0.1.fwsc",
                "tag": "v0.1",
            }
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
    print(f"catalogue xianqiang {xq_sha[:12]} + official V15 {v15_sha[:12]}")


if __name__ == "__main__":
    main()
