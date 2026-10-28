import { defineConfig } from 'vitest/config';

export default defineConfig({
  resolve: {
    alias: {
      '@simul/core': new URL('./packages/simul-core/src/index.ts', import.meta.url).pathname,
      '@simul/renderer': new URL('./packages/simul-renderer/src/index.ts', import.meta.url).pathname,
      '@simul/cloud': new URL('./packages/simul-cloud/src/index.ts', import.meta.url).pathname,
    },
  },
  test: {
    include: ['packages/**/tests/**/*.test.ts'],
    environment: 'node',
  },
});
