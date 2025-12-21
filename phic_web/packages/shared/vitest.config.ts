import { defineConfig } from 'vitest/config'

export default defineConfig({
  test: {
    globals: true,
  },
  esbuild: {
    target: 'es2016', // ES2016 supports ** operator
  },
})
