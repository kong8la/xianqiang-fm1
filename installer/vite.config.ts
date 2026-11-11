import { defineConfig } from "vitest/config";

// This file is typechecked by the DOM tsconfig, which does not load Node types.
declare const process: { env: { GITHUB_PAGES?: string } };

// GitHub Pages project site: https://kong8la.github.io/xianqiang-fm1/
// Local dev and tests keep relative URLs so `catalogue.json` and `pkg/` still resolve.
const pagesBase = process.env.GITHUB_PAGES === "true" ? "/xianqiang-fm1/" : "./";

export default defineConfig({
  base: pagesBase,
  server: { host: "127.0.0.1", port: 43127 },
  preview: { host: "127.0.0.1", port: 43127 },
  test: {
    include: ["tests/**/*.test.ts"],
    testTimeout: 20000,
  },
});
