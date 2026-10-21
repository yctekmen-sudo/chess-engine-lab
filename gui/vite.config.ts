import { defineConfig } from "vitest/config";
export default defineConfig({
  build: { outDir: "dist" },
  server: { host: "127.0.0.1" },
  test: {
    environment: "jsdom",
    include: ["test/**/*.test.tsx"],
    setupFiles: ["test/setup.ts"],
  },
});
