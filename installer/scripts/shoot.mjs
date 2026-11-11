import { chromium } from "playwright-core";
import { mkdir } from "node:fs/promises";

const base = process.env.XQ_URL || "http://127.0.0.1:43127/";
const out = "/opt/cursor/artifacts";
await mkdir(out, { recursive: true });

const browser = await chromium.launch({
  executablePath: process.env.CHROME || "/usr/local/bin/google-chrome",
  headless: true,
  args: ["--no-sandbox", "--disable-gpu"],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const errors = [];
page.on("pageerror", (err) => errors.push(String(err)));
await page.goto(base, { waitUntil: "networkidle" });
await page.waitForSelector("text=道哥合成器");
await page.waitForSelector("text=恢复官方 V15");
const opening = await page.locator("body").innerText();
for (const banned of ["Felucca", "Dinghy", "Baud Girl", "Groove OS", "FM1 Open", "OberVave", "仅外部链接"]) {
  if (opening.includes(banned)) throw new Error(`page still shows ${banned}`);
}
await page.screenshot({ path: `${out}/installer-catalogue.png`, fullPage: true });

await page.getByRole("button", { name: "演示 0xB36014" }).click();
await page.waitForSelector("text=FM-1_941");
await page.screenshot({ path: `${out}/installer-connected.png` });

await page.getByRole("button", { name: "新闪存不可装" }).first().click();
await page.waitForSelector("text=已拒绝安装");
await page.screenshot({ path: `${out}/installer-refuse-new-flash.png` });
await page.getByRole("button", { name: "关闭" }).click();

await page.locator("article.featured").getByRole("button", { name: "安装" }).click();
await page.waitForSelector("text=正在写入");
await page.screenshot({ path: `${out}/installer-progress.png` });
await page.waitForSelector("text=安装完成", { timeout: 15000 });
await page.screenshot({ path: `${out}/installer-done.png` });
await page.getByRole("button", { name: "关闭" }).click();

await page.locator("#rescue").scrollIntoViewIfNeeded();
await page.screenshot({ path: `${out}/installer-rescue.png` });

const phone = await browser.newPage({ viewport: { width: 390, height: 844 } });
await phone.goto(base, { waitUntil: "networkidle" });
await phone.waitForSelector("text=道哥合成器");
await phone.screenshot({ path: `${out}/installer-mobile.png`, fullPage: true });

if (errors.length) {
  console.error(errors);
  process.exitCode = 1;
}
await browser.close();
console.log("shots ok");
