import { defineConfig } from 'vite';

export default defineConfig({
  resolve: {
    alias: {
      '@simul/core': new URL('../../packages/simul-core/src/index.ts', import.meta.url).pathname,
      '@simul/renderer': new URL('../../packages/simul-renderer/src/index.ts', import.meta.url).pathname,
      '@simul/cloud': new URL('../../packages/simul-cloud/src/index.ts', import.meta.url).pathname,
    },
  },
  server: {
    host: '0.0.0.0',
    port: 5173,
    strictPort: true,
    allowedHosts: true,
  },
  preview: {
    host: '0.0.0.0',
    port: 5173,
    allowedHosts: true,
  },
});
