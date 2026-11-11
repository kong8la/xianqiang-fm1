import { defineConfig } from "vitest/config";

export default defineConfig({
  base: "./",
  server: { host: "127.0.0.1", port: 43127 },
  preview: { host: "127.0.0.1", port: 43127 },
  test: {
    include: ["tests/**/*.test.ts"],
    testTimeout: 20000,
  },
});
