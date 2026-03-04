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

---

# Phic Web 运行时

本目录包含围绕 C++ 核心 WASM 运行时的 TypeScript + Vite Web 壳。

## 当前功能

- 上传或拖放谱面文件（`.json/.rpe/.pec`）
- 可选的 advance JSON 摄入（轨道/顺序播放）
- 播放列表过滤 + 随机/种子/数量限制
- 全局 respack zip 加载（`info.yml`、note 纹理、hitfx 元数据、可选音效）
- Canvas2D 帧渲染
- C API 自动模拟步进（`phic_engine_step_auto`）
- 可选音频文件时钟同步（控制播放节奏）
- 基于 respack 的 note 渲染（`*_mh` 变体、hold atlas 切片、`holdRepeat`、`holdTailNoScale`、`holdKeepHead`、`holdCompact`、hitfx 帧动画/染色/旋转）
- 判定事件驱动的击打反馈 + note 种类打击音播放
- WASM ABI v5 路径（含旧版兼容回退），支持更丰富的帧/判定载荷
- 停止按钮（终止当前运行）

## 构建 WASM

在仓库根目录使用 Emscripten 工具链：

```bash
emcmake cmake -S phic_port -B phic_port/build-web -DPHIC_BUILD_WEB=ON -DPHIC_BUILD_DESKTOP_APP=OFF -DPHIC_BUILD_TESTS=OFF
cmake --build phic_port/build-web -j
```

预期输出：

- `phic_web/wasm/phic_web.js`
- `phic_web/wasm/phic_web.wasm`

## 运行 Web 应用

```bash
cd phic_web
npm install
npm run dev
```

打开 Vite 显示的 URL，在界面中加载谱面文件即可。
