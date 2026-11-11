#!/usr/bin/env python3
"""Build installer/public/catalogue.json and mirror .fwsc packages.

GitHub release downloads redirect without Access-Control-Allow-Origin, so a
page on another host cannot fetch them. Packages whose licence allows
redistribution are mirrored next to the site. SHA-256 is computed here from
the bytes we downloaded, not copied from a third-party list.

Flash compatibility (new = UC25HQ80 / 0xB36014, old = Puya only, any = both
or chip-agnostic) follows The Firmware Hub's public catalogue
(https://thefirmwarehub.com/catalog.json, CC BY 4.0, read 2026-11-11).
Name the hub when you republish that classification.
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

# id, repo, flash, licence, one-line Chinese description
SEEDS = [
    ("felucca", "hugelton/Felucca", "new", "GPL-3.0-only", "多引擎开源固件，社区大多数第三方固件的起点。"),
    ("dinghy", "zvenson/dinghy", "new", "GPL-3.0-only", "最小的可再开发底板：更新、救援、MIDI 和屏幕都已接好，上面是一个正弦。"),
    ("obervave", "zvenson/obervave", "new", "GPL-3.0-only", "在 Dinghy 一脉上的演奏固件，已认较新的 UC25HQ80 闪存。"),
    ("sloopdx", "zvenson/dxsloop", "new", "GPL-3.0-only", "带 DX7 的 SLOOP，同样认 0xB36014。"),
    ("sloop", "isod89/sloop-fm1", "old", "GPL-3.0-only", "现场律动盒。这一版只认早期 Puya 闪存。"),
    ("melodee", "keremimo/melodee", "new", "GPL-3.0-only", "微分音、CZ-1 与六算子 FM。"),
    ("x0x", "charlesvestal/fm1-x0x", "new", "GPL-3.0-only", "909/808 鼓机和两台 303。"),
    ("fomni", "charlesvestal/fm1-fomni", "new", "GPL-3.0-only", "和弦竖琴。"),
    ("amb1", "charlesvestal/fm1-amb", "new", "MPL-2.0", "生成式环境音乐，气质接近 Music for Airports。"),
    ("ctl1", "charlesvestal/fm1-ctl", "new", "MPL-2.0", "带自己声音的 MIDI 控制器。"),
    ("polly", "charlesvestal/fm1-polly", "new", "GPL-3.0-only", "口袋编曲机，乐队跟着你的和弦走。"),
    ("skiff", "illuminus5/skiff", "new", "GPL-3.0-only", "六轨、十四个引擎。"),
    ("duell", "oscillr/DUELL", "new", "GPL-3.0-only", "双音色渐变合成器。"),
    ("fm106", "PatttF/fm-106", "new", "GPL-3.0-only", "Juno-106 风格的复音合成。"),
    ("choralroot", "Quixotic7/ChoralRootFM1", "new", "GPL-3.0-only", "和弦乐器。"),
    ("fimba", "jadamsowers/fm1-fimba", "new", "GPL-3.0-only", "卡林巴。"),
    ("hortator", "deadactive/hortator", "new", "GPL-3.0-only", "八轨鼓机。"),
    ("clipper", "isod89/clipper-fm1", "new", "GPL-3.0-only", "Felucca 家族上的另一台乐器。"),
    ("etch", "SikkenWorks/ETCH", "any", "GPL-3.0-only", "自己画、再渐变的波形。"),
    ("b1", "SikkenWorks/B1-Drawbar-Organ", "any", "GPL-3.0-only", "拉杆风琴。"),
    ("bubba", "Erbubar23/bubba-machine", "any", "GPL-3.0-only", "从零写的六轨律动盒。"),
    ("zp12", "zvenson/zp12", "old", "GPL-3.0-only", "12 比特采样鼓机，气质接近 SP-1200。只认早期闪存。"),
    ("salt", "ChanceTheMaker/Felucca", "old", "GPL-3.0-only", "Felucca 的 Salt 分支，带浏览器工作室。只认早期闪存。"),
    ("orbit", "dspaudio/orbit", "old", "GPL-3.0-only", "四轨律动盒，屏幕接近 OP-1 的分格。只认早期闪存。"),
    ("ghoulbox", "jasonpersinger/ghoulbox-fm1-dungeon-synth", "old", "GPL-3.0-only", "地牢合成器，带教堂混响。只认早期闪存。"),
    ("rainbow", "joshbartlettband-web/felucca-rainbow", "new", "GPL-3.0-only", "给孩子的音乐玩具。"),
    ("patterns", "echoe/Felucca_8_patterns", "new", "GPL-3.0-only", "Felucca，每条音轨八条序列。"),
]

LINKS = [
    {
        "id": "baud-girl",
        "kind": "link",
        "name": "Baud Girl FM-1+VA",
        "author": "Madeline",
        "summary": "虚拟模拟、8 比特和 FM，加带和弦的音序器。没有公开源码，不能在这里转发安装包。",
        "license": "未公开源码",
        "href": "https://baudgirl.com/work/FM-1+VA/install",
        "source": "https://baudgirl.com/work/FM-1+VA/install",
    },
    {
        "id": "groove-os",
        "kind": "link",
        "name": "Groove OS",
        "author": "Groove OS",
        "summary": "把 FM-1 变成采样律动盒。专有软件，安装在作者自己的页面。",
        "license": "专有",
        "href": "https://www.groove-os.com/install",
        "source": "https://www.groove-os.com/",
    },
    {
        "id": "fm1-open",
        "kind": "link",
        "name": "FM1 Open 工作台",
        "author": "M-VAVE",
        "summary": "官方开源应用配闭源引擎 SDK。没有可转发的独立安装包，在 M-VAVE 工作台里更新。",
        "license": "应用 GPL-3.0，引擎为二进制 SDK",
        "href": "https://open.m-vave.com/fm1-workbench",
        "source": "https://github.com/m-vave/fm1_open",
    },
]

V15_URL = (
    "https://raw.githubusercontent.com/m-vave/fm1_open/"
    "b178ac43c25f010c60a878056fb5331fe693fd62/sdk/firmware/FM-1_015.fwsc"
)
V15_SHA = "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a"


def get(url: str) -> bytes:
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=90) as res:
        return res.read()


def choose_asset(rel: dict) -> dict | None:
    assets = rel.get("assets") or []
    fw = [a for a in assets if a["name"].lower().endswith(".fwsc") and "diag" not in a["name"].lower()]
    plain = [a for a in fw if all(x not in a["name"].lower() for x in ("smk", "elite", "mke"))]
    pick = plain or fw
    if not pick:
        return None
    pick.sort(key=lambda a: (a["name"].count("-"), len(a["name"])))
    return pick[0]


def latest_asset(repo: str) -> tuple[str, str, str]:
    rels: list[dict] = []
    try:
        rels.append(json.loads(get(f"https://api.github.com/repos/{repo}/releases/latest")))
    except Exception:
        pass
    listed = json.loads(get(f"https://api.github.com/repos/{repo}/releases?per_page=8"))
    rels.extend(listed)
    seen = set()
    for rel in rels:
        tag = rel.get("tag_name") or ""
        if tag in seen:
            continue
        seen.add(tag)
        asset = choose_asset(rel)
        if asset:
            return tag, asset["name"], asset["browser_download_url"]
    raise RuntimeError("no .fwsc asset")


def main() -> None:
    PKG.mkdir(parents=True, exist_ok=True)
    entries = []

    xq_src = ROOT.parents[0] / "dist" / "xianqiang-0.1.fwsc"
    xq = xq_src.read_bytes()
    (PKG / "xianqiang-0.1.fwsc").write_bytes(xq)
    entries.append({
        "id": "xianqiang",
        "kind": "install",
        "featured": True,
        "name": "弦腔 XianQiang",
        "author": "本仓库",
        "summary": "六复音物理建模：拨弦、木棒、钟、鼓膜、拉弦和单簧管/长笛。认 Puya 与 UC25HQ80。",
        "license": "GPL-3.0-only",
        "version": "0.1",
        "source": None,
        "flash": "new",
        "product": "FM-1_941",
        "sha256": hashlib.sha256(xq).hexdigest(),
        "size": len(xq),
        "file": "pkg/xianqiang-0.1.fwsc",
        "tag": "v0.1",
    })

    v15 = get(V15_URL)
    digest = hashlib.sha256(v15).hexdigest()
    if digest != V15_SHA:
        raise SystemExit(f"V15 hash {digest} != {V15_SHA}")
    (PKG / "FM-1_015.fwsc").write_bytes(v15)
    official = {
        "id": "v15",
        "kind": "official",
        "name": "官方 V15",
        "author": "M-VAVE",
        "summary": "回到 M-VAVE 发布在 fm1_open 里的 FM-1_015。写入前按 SHA-256 核对。",
        "license": "Apache-2.0",
        "version": "015",
        "source": "https://github.com/m-vave/fm1_open",
        "flash": "old",
        "product": "FM-1_015",
        "sha256": digest,
        "size": len(v15),
        "file": "pkg/FM-1_015.fwsc",
        "tag": "b178ac4",
    }

    for sid, repo, flash, lic, summary in SEEDS:
        try:
            tag, name, url = latest_asset(repo)
            blob = get(url)
        except Exception as e:  # noqa: BLE001 — one bad release should not drop the rest
            print(f"skip {repo}: {e}")
            continue
        if b"FELUCCA-LOADER-1" not in blob:
            print(f"skip {sid}: package has no Felucca loader, not mirrored")
            continue
        path = PKG / f"{sid}.fwsc"
        path.write_bytes(blob)
        author = repo.split("/")[0]
        entries.append({
            "id": sid,
            "kind": "install",
            "featured": False,
            "name": sid,
            "author": author,
            "summary": summary,
            "license": lic,
            "version": tag.lstrip("v"),
            "source": f"https://github.com/{repo}",
            "flash": flash,
            "product": None,
            "sha256": hashlib.sha256(blob).hexdigest(),
            "size": len(blob),
            "file": f"pkg/{sid}.fwsc",
            "tag": tag,
            "asset": name,
            "download": url,
        })
        print(f"ok {sid} {tag} {len(blob)} {entries[-1]['sha256'][:12]}")

    # Pretty names for the well-known ids (the release tag is the version).
    titles = {
        "felucca": "Felucca", "dinghy": "Dinghy", "obervave": "OberVave", "sloopdx": "sloopDX",
        "sloop": "SLOOP", "melodee": "Melodee", "x0x": "X0X", "fomni": "FoMni", "amb1": "AMB-1",
        "ctl1": "CTL-1", "polly": "Polly", "skiff": "Skiff", "duell": "DUELL", "fm106": "FM-106",
        "choralroot": "ChoralRoot", "fimba": "FiMba-1", "hortator": "Hortator", "clipper": "Clipper",
        "etch": "ETCH", "b1": "B1 拉杆风琴", "bubba": "Bubba Machine", "zp12": "zp12",
        "salt": "Felucca Salt", "orbit": "ORBIT", "ghoulbox": "GHOULBOX", "rainbow": "Rainbow Mode",
        "patterns": "Felucca 8 Patterns",
    }
    for e in entries:
        e["name"] = titles.get(e["id"], e["name"])

    doc = {
        "generatedBy": "installer/scripts/fetch-catalogue.py",
        "flashNote": "new/old/any 的闪存分类来自 The Firmware Hub 公开目录（CC BY 4.0，https://thefirmwarehub.com ），2026-11-11。安装包本身从各仓库的 GitHub Release 下载并在本地计算 SHA-256。",
        "attribution": "Firmware list flash tags: The Firmware Hub – https://thefirmwarehub.com",
        "firmwares": entries,
        "official": official,
        "links": LINKS,
    }
    (PUBLIC / "catalogue.json").write_text(json.dumps(doc, ensure_ascii=False, indent=2) + "\n")
    print(f"catalogue {len(entries)} installable + official V15 + {len(LINKS)} links")


if __name__ == "__main__":
    main()
