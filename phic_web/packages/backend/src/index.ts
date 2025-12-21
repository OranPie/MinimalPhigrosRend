// Server entry point
import { startServer } from './server.js'

const PORT = parseInt(process.env.BACKEND_PORT || '3000', 10)
const HOST = process.env.BACKEND_HOST || '0.0.0.0'

async function main() {
  try {
    await startServer({ port: PORT, host: HOST })
    console.log(`🚀 Backend server listening on http://${HOST}:${PORT}`)
  } catch (err) {
    console.error('Failed to start server:', err)
    process.exit(1)
  }
}

main()

// Export parsers for use in frontend
export * from './parsers/index.js'
