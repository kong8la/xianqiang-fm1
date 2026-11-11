# 弦腔安装器

线上地址：[https://kong8la.github.io/xianqiang-fm1/](https://kong8la.github.io/xianqiang-fm1/)

在 Chrome 或 Edge 里给 M-VAVE FM-1 换固件的静态网页。走的是和 M-VAVE 更新器相同的 USB-MIDI SysEx：正在运行的固件先把包里的更新加载器读走并启动，加载器再把整包写进应用区。这个页面只回答设备的读取请求，不直接写闪存地址。源码仓库：[kong8la/xianqiang-fm1](https://github.com/kong8la/xianqiang-fm1)。

## 本地看

```sh
cd installer
python3 scripts/fetch-catalogue.py   # 已生成 public/catalogue.json 和 public/pkg/ 时可跳过
npm install
npm test
npm run dev                          # http://127.0.0.1:43127
```

没有真机时，页面上的「演示 0xB36014」会接一台模拟 FM-1：它按同一套协议握手、读包、进入加载器、写完再报新身份。模拟机不会碰硬件。

`npm test` 用 Vitest 核对 7 比特打包、请求校验、和仓库里 Python 安装器对同一段字节的结果、弦腔 0.1 包的身份 `FM-1_941`，以及一次从「正在运行」到「写完重启」的模拟安装。读到超出包长度的地址会直接拒绝，不发送数据。

## 部署

推到 `main` 后，`.github/workflows/pages.yml` 会跑测试、以 `GITHUB_PAGES=true` 构建（Vite `base` 为 `/xianqiang-fm1/`），核对 `installer/dist/pkg/` 里每个包的 SHA-256 仍与目录一致，再用 `actions/upload-pages-artifact` 和 `actions/deploy-pages` 发到 GitHub Pages。页面和 `pkg/*.fwsc` 同源，哈希校验不依赖 GitHub Release 的跨域头。

第一次要在仓库里打开 Pages，来源选 **GitHub Actions**（Settings → Pages → Build and deployment → Source）。仓库需为公开，免费计划才能用 Pages。私有仓库要付费计划，否则 Actions 部署会被拒。

也可以自己托管 `installer/dist/`：

```sh
cd installer
npm test
GITHUB_PAGES=true npm run build    # 给 kong8la.github.io/xianqiang-fm1/
npm run build                       # 本地或网站根目录，base 为 ./
```

- **Vercel / Netlify / Cloudflare Pages**：根目录设为 `installer`，构建命令 `npm run build`，输出目录 `dist`。若站点不在域名根路径，把构建环境变量 `GITHUB_PAGES` 设成 `true`，或改 `vite.config.ts` 里的 `base`。
- **自己的 nginx**：`root` 指到 `dist`。`.fwsc` 用默认二进制类型即可。

GitHub Release 的下载地址会 302 到 `release-assets.githubusercontent.com`，响应里没有 `Access-Control-Allow-Origin`，浏览器里的页面不能直接去拉。所以可再分发的包放在 `public/pkg/`，由 `scripts/fetch-catalogue.py` 从各仓库的最新 Release 下载并计算 SHA-256。官方 V15 来自 `m-vave/fm1_open` 提交 `b178ac4` 的 `sdk/firmware/FM-1_015.fwsc`，哈希必须是 `db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a`。

没有 Felucca 更新加载器标记（`FELUCCA-LOADER-1`）的社区包不会被放进目录。这个标记用来确认加载器不会改 `0x0000`–`0x3FFF` 的引导区。官方 V15 是例外：它用自己的加载器，只接受上面这一个哈希。

## 目录里有什么

`public/catalogue.json` 把弦腔放在第一张卡片。其余是 GPL-3.0 或 MPL-2.0 的 FM-1 固件，各一条中文说明、作者、许可证、版本、源码链接、SHA-256，以及是否认较新的 UC25HQ80（`0xB36014`）。

`new` / `old` / `any` 这档闪存分类参考了 [The Firmware Hub](https://thefirmwarehub.com) 的公开目录（CC BY 4.0，2026-11-11 读取）。固件说明是这里写的，包是从作者的 GitHub Release 拉下来后自己算的哈希。转载闪存分类时请写上：Firmware list flash tags: The Firmware Hub – https://thefirmwarehub.com 。

Baud Girl、Groove OS、FM1 Open 工作台没有可以再分发的安装包，目录里只放外链。

zp12 和 Rainbow Mode 的仓库当时没有可下载的 `.fwsc` Release，所以没有镜像。

## 安全

- 写入前核对 SHA-256，并解析包身份。身份不是 `FM-1_…` 的包会拒绝。
- 文件头、文件表和 `flash.bin` / `ota.bin` 的 CRC 不对就拒绝。
- 设备如果请求包以外的地址，页面不回答。
- 握手只报告固件身份（例如 `FM-1_941`），不报告闪存 JEDEC。页面请你自己选 `0x856014` 或 `0xB36014`。选了新闪存之后，只认早期 Puya 的固件按钮会打开拒绝说明，不会发更新命令。还没选时，安装这类固件会先弹出警告，要你确认这台不是 `0xB36014`。
- 更新协议本身不让这个网页指定「把引导区写成某某字节」。社区包还要求带 Felucca 加载器；官方 V15 只接受固定哈希。

刷写前用 [M-Flasher](https://github.com/jvitkauskas/m-flasher) 或 [MvaveFM1Unbricker](https://github.com/Quixotic7/MvaveFM1Unbricker) 备份整片 1 MiB。下载模式里出现的 `WL82 UBOOT1.00` 磁盘不要格式化。救砖步骤在页面底部：开机按住 OCT−、ROM 下载模式、RP2040 USB_KEY（`0x16EF`，见 [FM-1 Transporter](https://github.com/kurogedelic/FM-1-transporter)）。不要在 UC25HQ80 上安装 m-bootloader。

## 没有真机时没法核对的事

模拟安装走的是同一套组帧和状态机，但没有 USB 线、没有真实的加载器时序，也没有在 Puya 或 UC25HQ80 上刷过这一页。真机上如果写到一半拔线，重新连接后再装同一个包，加载器还在的话会接着写。
