import Fastify, { FastifyInstance } from 'fastify'
import fastifyCors from '@fastify/cors'
import fastifyStatic from '@fastify/static'
import fastifyMultipart from '@fastify/multipart'
import path from 'path'
import { fileURLToPath } from 'url'

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)

export interface ServerOptions {
  port: number
  host: string
}

export async function startServer(opts: ServerOptions): Promise<FastifyInstance> {
  const fastify = Fastify({
    logger: {
      level: process.env.LOG_LEVEL || 'info',
      transport: {
        target: 'pino-pretty',
        options: {
          translateTime: 'HH:MM:ss Z',
          ignore: 'pid,hostname'
        }
      }
    }
  })

  // CORS
  await fastify.register(fastifyCors, {
    origin: process.env.CORS_ORIGIN || 'http://localhost:5173',
    credentials: true
  })

  // Multipart for file uploads
  await fastify.register(fastifyMultipart, {
    limits: {
      fileSize: parseInt(process.env.MAX_UPLOAD_SIZE || '104857600', 10) // 100MB default
    }
  })

  // Serve Python charts directory
  const pythonChartsDir = path.resolve(__dirname, process.env.PYTHON_CHARTS_DIR || '../../../charts')
  await fastify.register(fastifyStatic, {
    root: pythonChartsDir,
    prefix: '/assets/charts/',
    decorateReply: false
  })

  // Health check
  fastify.get('/health', async () => {
    return { status: 'ok', timestamp: new Date().toISOString() }
  })

  // API routes placeholder
  fastify.get('/api/charts', async () => {
    return { charts: [] }
  })

  await fastify.listen({ port: opts.port, host: opts.host })
  return fastify
}
