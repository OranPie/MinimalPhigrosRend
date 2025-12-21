#!/bin/bash
# Build script for phic_web

set -e

echo "🏗️  Building phic_web..."

# Build shared first (dependency for backend and frontend)
echo "📦 Building @phic-web/shared..."
pnpm --filter @phic-web/shared build

# Build backend and frontend in parallel
echo "📦 Building backend and frontend..."
pnpm --filter @phic-web/backend build &
pnpm --filter @phic-web/frontend build &

# Wait for both builds to complete
wait

echo "✅ Build complete!"
echo "   Backend:  packages/backend/dist"
echo "   Frontend: packages/frontend/dist"
