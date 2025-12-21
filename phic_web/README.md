# phic_web

Browser-based interactive Phigros chart player with TypeScript backend and PixiJS frontend.

## Features

- Interactive web player with manual input and judge system
- TypeScript backend with Chart CRUD API
- PixiJS rendering engine for 60+ FPS performance
- Judge improvements: timing accuracy, multi-touch detection, calibration
- Seamless integration with existing Python codebase

## Project Structure

```
phic_web/
├── packages/
│   ├── backend/      # TypeScript REST API server (Fastify)
│   ├── frontend/     # PixiJS + React web app (Vite)
│   └── shared/       # Shared types and utilities
└── scripts/          # Development and build scripts
```

## Prerequisites

- Node.js >= 18.0.0
- pnpm >= 8.0.0

## Quick Start

```bash
# Install dependencies
pnpm install

# Start development servers (backend + frontend)
pnpm dev

# Build all packages
pnpm build

# Run tests
pnpm test
```

## Development

### Backend (port 3000)
```bash
cd packages/backend
pnpm dev
```

### Frontend (port 5173)
```bash
cd packages/frontend
pnpm dev
```

## Environment Setup

Copy `.env.example` to `.env` and adjust values as needed:
```bash
cp .env.example .env
```

## Documentation

See the plan file at `~/.claude/plans/tranquil-sleeping-nest.md` for detailed architecture and implementation details.

## License

Same as parent MinimalPhigrosRend project.
