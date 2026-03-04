# C++ Core Port (Phase 1 Scaffold)

This directory contains a C++ core-first implementation scaffold for the Phic renderer migration plan.

## Included

- `core/`: portable engine, nlohmann-json parser, mod pipeline, judgment loop, frame-command generation
- `core_c_api/`: stable C ABI facade for native and web wrappers (includes auto-simulate step)
- `desktop_app/`: CLI runner with merged input/advance/playlist execution flow
- `../phic_web/`: TypeScript + Vite web shell around WASM runtime
- `tests/`: baseline engine/parser/mod tests

## Native Build

```bash
cmake -S phic_port -B phic_port/build
cmake --build phic_port/build -j
ctest --test-dir phic_port/build --output-on-failure
```

## Web Build (Emscripten)

```bash
emcmake cmake -S phic_port -B phic_port/build-web -DPHIC_BUILD_WEB=ON -DPHIC_BUILD_DESKTOP_APP=OFF -DPHIC_BUILD_TESTS=OFF
cmake --build phic_port/build-web -j
```

Web artifacts are emitted to `phic_web/wasm/`.

## Current status

- Judgment windows/weights aligned with Python constants (`0.045/0.090/0.150`, weights `1.0/0.6/0/0`)
- Note kind IDs aligned with Python (`tap=1, drag=2, hold=3, flick=4`) with format-aware parsing (Official vs RPE type ids)
- Official parser applies BPM unit-time conversion; RPE parser applies beat/BPM map conversion with `bpmfactor`
- JSON chart parsing uses `nlohmann::json`
- JSONC config preprocessing supports comments + trailing commas
- Core mods support mirror/reverse/randomize/thin_out/transpose/stretch/quantize/wave/stutter/hold_convert
- Core mods now also support `full_blue`, lane-scale (`scale.x`), `compress_zip`, `attach` (lane/time subset), `fade` (time/constant), and note rules/overrides subset (`kind`/`speed_mul`/`alpha`/`side`)
- Simulateplay auto-input is available in C API via `phic_engine_step_auto`
- Judge events are exposed in C API via `phic_engine_step_ex` / `phic_engine_step_auto_ex`
- ABI v5 adds `phic_engine_step_v2` / `phic_engine_step_auto_v2` with `note_kind` and hold timing metadata
- Test suite includes a Python-vs-C++ parity oracle (`phic_parity_oracle`)
- Desktop run planner merges single input + advance tracks + playlist discovery

## Next steps

- Expand parser compatibility against full Official/RPE/PEC fixture corpus
- Add WebGL renderer option after Canvas2D baseline stabilizes
- Add browser integration tests (Playwright)
- Implement Windows-compatible ffmpeg process piping

---

# C++ 核心移植（第一阶段脚手架）

本目录包含 Phic 渲染器迁移计划的 C++ 核心优先实现脚手架。

## 目录结构

- `core/`: 可移植引擎、nlohmann-json 解析器、Mod 流水线、判定循环、帧命令生成
- `core_c_api/`: 用于原生与 Web 封装的稳定 C ABI 外观（含自动模拟步进）
- `desktop_app/`: 融合单谱面 / advance / 播放列表发现的 CLI 运行器
- `../phic_web/`: 围绕 WASM 运行时的 TypeScript + Vite Web 壳
- `tests/`: 基准引擎 / 解析器 / Mod 测试

## 原生构建

```bash
cmake -S phic_port -B phic_port/build
cmake --build phic_port/build -j
ctest --test-dir phic_port/build --output-on-failure
```

## Web 构建（Emscripten）

```bash
emcmake cmake -S phic_port -B phic_port/build-web -DPHIC_BUILD_WEB=ON -DPHIC_BUILD_DESKTOP_APP=OFF -DPHIC_BUILD_TESTS=OFF
cmake --build phic_port/build-web -j
```

Web 产物输出到 `phic_web/wasm/`。

## 当前状态

- 判定窗口与权重已与 Python 常量对齐（`0.045/0.090/0.150`，权重 `1.0/0.6/0/0`）
- Note kind ID 已与 Python 对齐（`tap=1, drag=2, hold=3, flick=4`），支持格式感知解析（Official / RPE 类型 ID）
- Official 解析器已应用 BPM 单位时间换算；RPE 解析器已应用 beat/BPM 图 + `bpmfactor` 换算
- JSON 谱面解析使用 `nlohmann::json`
- JSONC 配置预处理支持注释与尾部逗号
- 核心 Mod 支持：mirror / reverse / randomize / thin_out / transpose / stretch / quantize / wave / stutter / hold_convert
- 核心 Mod 还支持：`full_blue`、lane-scale（`scale.x`）、`compress_zip`、`attach`（车道/时间子集）、`fade`（时间/常量）、note rules/overrides 子集（`kind`/`speed_mul`/`alpha`/`side`）
- C API 中可通过 `phic_engine_step_auto` 使用模拟自动游玩输入
- C API 通过 `phic_engine_step_ex` / `phic_engine_step_auto_ex` 暴露判定事件
- ABI v5 新增 `phic_engine_step_v2` / `phic_engine_step_auto_v2`，附带 `note_kind` 和 Hold 时间元数据
- 测试套件包含 Python 与 C++ 对比验证器（`phic_parity_oracle`）
- 桌面运行规划器融合了单谱面 + advance 轨道 + 播放列表发现

## 下一步

- 针对完整 Official/RPE/PEC 样本语料库扩展解析器兼容性
- Canvas2D 基线稳定后添加 WebGL 渲染器选项
- 添加浏览器集成测试（Playwright）
- 实现 Windows 兼容的 ffmpeg 进程管道
