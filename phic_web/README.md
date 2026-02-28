# Phic Web Runtime

This folder contains the TypeScript + Vite web shell for the C++ core WASM runtime.

## Features in current web shell

- Upload or drag-drop chart files (`.json/.rpe/.pec`)
- Optional advance JSON ingestion (tracks/sequence)
- Playlist filtering + shuffle/seed/limit
- Global respack zip loading (`info.yml`, note textures, hitfx metadata, optional sounds)
- Canvas2D frame rendering
- C API auto-simulate stepping (`phic_engine_step_auto`)
- Optional audio-file clock sync for playback pacing
- Respack-driven note rendering (`*_mh` variants, hold atlas slicing, `holdRepeat`, `holdTailNoScale`, `holdKeepHead`, `holdCompact`, hit-fx sheet animation/tint/rotate)
- Judge-event driven hit feedback + note-kind hitsound playback
- WASM ABI v5 path (with legacy fallback) for richer frame/judge payloads
- Stop button for run cancellation

## Build WASM

Use Emscripten toolchain from repo root:

```bash
emcmake cmake -S phic_port -B phic_port/build-web -DPHIC_BUILD_WEB=ON -DPHIC_BUILD_DESKTOP_APP=OFF -DPHIC_BUILD_TESTS=OFF
cmake --build phic_port/build-web -j
```

Expected output:

- `phic_web/wasm/phic_web.js`
- `phic_web/wasm/phic_web.wasm`

## Run Web App

```bash
cd phic_web
npm install
npm run dev
```

Then open the Vite URL and load files in the UI.
