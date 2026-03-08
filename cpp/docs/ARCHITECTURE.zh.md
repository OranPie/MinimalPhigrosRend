# 架构概览

> 🌐 [English](ARCHITECTURE.md)
```
cpp/
├── include/phigros/       # All headers (header-only core)
│   ├── chart/             # Chart format parsers
│   ├── math/              # Easing, tracks, geometry
│   ├── core/              # Shared types
│   ├── engine/            # Chart simulation engine
│   ├── config/            # Config loading/saving
│   ├── hud/               # HUD state
│   ├── render/            # Frame building + rendering
│   ├── io/                # Respack, replay, video export
│   └── app/               # Application layer (game loop, input)
├── src/
│   └── app/main.cpp       # CLI entry point (~200 lines)
├── tests/
│   ├── test_engine.cpp    # Unit tests (6343 checks)
│   └── test_parser.cpp    # Parser tests (54 checks)
└── vendor/                # nlohmann/json, miniz, stb, miniaudio
```

---

## 数据流

```
CLI args + config.jsonc
        │
        ▼
   RenderConfig
        │
        ▼
  Chart parser  ──►  ChartData  { lines[], notes[] }
                          │
              ┌───────────┼────────────┐
              ▼           ▼            ▼
       NoteManager   SimulatePlayer  ManualJudge
       (visibility)  (autoplay)      (--play mode)
              │           │               ▲
              │           │               │
              │           │         JudgeInputFrame
              │           │         (from InputManager)
              └─────┬─────┘
                    ▼
              engine::Judge
              (acc_sum, combo)
                    │
                    ▼
           build_frame(t, ...)
                    │
                    ▼
             FrameSnapshot  { lines[], notes[], hud }
                    │
              ┌─────┼─────┐
              ▼     ▼     ▼
         LineRen  NoteRen  HoldRen  HitFXRen  HudRen
              │
              ▼
          SpriteBatch  (SDL draw calls / bgfx executor)
              │
              ▼
           Window
```

---

## 模块参考

### `chart/` — 解析器

| 文件 | 说明 |
|------|------|
| `official_parser.hpp` | 解析 Phigros 官方 JSON 格式 |
| `rpe_parser.hpp` | 解析 RPE（Re:PhiEdit）JSON 格式 |
| `pec_parser.hpp` | 解析 PEC 二进制/文本格式 |
| `chart_loader.hpp` | 自动检测格式，调用对应的解析器 |
| `bpm_map.hpp` | `BpmMap` — 节拍到秒的换算（BPM 分段列表） |
| `compiled_chart.hpp` | `CompiledChartData` — 所有轨道预采样的单精度浮点平坦数组 |
| `compiler.hpp` | `compile_chart()` — 以固定频率对所有缓动轨道采样，生成 `CompiledChartData` |
| `phbc_io.hpp` | `write_phbc()` / `read_phbc()` — 二进制 `.phbc` 格式的往返序列化 |
| `sampled_track.hpp` | `SampledTrack` — O(1) 线性插值查找，基于均匀浮点采样数组 |

所有解析器均输出 `ChartData`：
```cpp
struct ChartData {
    std::vector<Line>   lines;
    std::vector<Note>   notes;  // sorted by t_hit
    ChartMeta           meta;
};
```

Notes 在解析后按 **`t_hit` 排序** — 这是二分查找可见性边界的前提。

### `math/` — 数学基元

| 文件 | 说明 |
|------|------|
| `easing.hpp` | 29 种缓动函数（与 RPE 索引对应） |
| `tracks.hpp` | `ConstTrack`、`LinearTrack`、`EasedTrack`、`IntegralTrack` — 分段求值，使用二分查找 `seek()` |
| `geometry.hpp` | `note_world_pos()` — 将谱面空间中的 Note 偏移量转换为屏幕空间 XY 坐标 |

`IntegralTrack` 用于 `scroll`（即 `speed_event * chart_speed` 的积分），是正确计算 Note 流位置的关键。

### `engine/` — 模拟

| 文件 | 说明 |
|------|------|
| `kinematics.hpp` | `LineState`、`eval_line_state()`、`note_world_pos_cs()`（预计算 cos/sin） |
| `judge.hpp` | `Judge` — 判定时间窗口（PERFECT=45ms，GOOD=90ms，BAD=150ms），`compute_score()` |
| `judge_input.hpp` | `JudgeAction`、`JudgeInputFrame` — 平台无关的判定输入抽象层 |
| `note_manager.hpp` | `precompute_t_enter()`，基于二分查找的可见性更新 |
| `hold_logic.hpp` | `hold_maintenance()`、`hold_finalize()`、`detect_misses()` |
| `simulateplay.hpp` | `SimulatePlayer` — 帧精度的自动演奏模拟 |
| `manual_judge.hpp` | `ManualJudge` — 交互模式下的空间+时间 Note 匹配；消费 `JudgeInputFrame`（指针：空间+时间匹配，键盘：仅时间匹配） |
| `effects.hpp` | `EffectManager` — `HitFX`、`FlashFX`、`ParticleBurst` 的生命周期管理 |
| `visibility.hpp` | `scroll_speed_at()`，`note_manager` 使用的 AABB 可见性检测辅助函数 |

#### 判定时间窗口常量

| 评级 | 窗口（±ms） | acc 权重 |
|------|------------|----------|
| PERFECT | 45 | 1.0 |
| GOOD | 90 | 0.6 |
| BAD | 150 | 0.0 |
| MISS | > 150 | 0.0 |

#### 得分公式

```
score = int( acc_sum/N × 900000 + max_combo/N × 100000 )
```

### `render/` — 帧构建与绘制

| 文件 | 说明 |
|------|------|
| `renderer.hpp` | `build_frame()` — 将所有判定线/Note 状态求值并写入 `FrameSnapshot` |
| `note_renderer.hpp` | 绘制 tap/drag/flick Note；应用 `size_px`、描边及 miss 淡出效果 |
| `hold_renderer.hpp` | 绘制 hold Note（头部/主体/尾部切片）；hold 发光叠加层 |
| `line_renderer.hpp` | 将判定线绘制为旋转矩形，并附带中心圆点 |
| `hitfx_renderer.hpp` | 绘制 `HitFX`（旋转精灵/动画圆环）及 `ParticleBurst` |
| `hud_renderer.hpp` | 通过 `FontAtlas` 绘制分数、连击数、准确率、进度条 |
| `bg_renderer.hpp` | 模糊背景及暗化叠加层 |
| `result_screen.hpp` | 谱面结束后的结算界面（分数、评级、准确率、连击数） |
| `pause_overlay.hpp` | 暂停状态的半透明叠加层及提示文字 |
| `trail_renderer.hpp` | 用于拖影/残影效果的 `RenderTarget` 循环缓冲槽 |
| `motion_blur.hpp` | 运动模糊的子帧累积 |
| `render_target.hpp` | `SDL_TEXTUREACCESS_TARGET` 封装 |

#### `build_frame()` 优化说明

1. **二分查找 Note 窗口**：仅遍历 `[t − 12s, t + approach×2]` 范围内的 Note
2. **栈上判定线数组**：`std::array<LineState, 256>`（无堆分配）
3. **预计算三角函数**：每条判定线仅计算一次 `LineState::cos_rot / sin_rot`；`note_world_pos_cs()` 在每个 Note 处跳过 `cos()/sin()` 调用
4. **自适应预留**：使用 `thread_local` 上一帧 Note 数量作为 `vector::reserve()` 的提示

### `config/` — 配置

`render_config.hpp` 包含 `RenderConfig`（普通结构体）、`load_config()`（JSONC 解析 + 值域截断）、`config_to_json()` 及 `save_config()`。

`LineAlphaMode` 枚举在热渲染路径中避免了字符串比较。

### `io/` — 输入/输出

| 文件 | 说明 |
|------|------|
| `respack.hpp` | 通过 miniz 加载 `respack.zip`；持有 Note 及 hitfx 纹理 |
| `replay.hpp` | `ReplayWriter`（记录事件）+ `ReplayPlayer`（回放）；使用 miniz 压缩的二进制格式 |
| `video_encoder.hpp` | `RecordingSession` — 将原始 RGBA 帧通过管道传输到 FFmpeg 子进程 |
| `audio.hpp` | `AudioSystem` — 基于 miniaudio 的 BGM 播放器，含偏移量补偿 |

#### Replay 格式

```
Header:  magic[4]  version[2]  chart_hash[4]  unix_ts[8]
Events:  [t:f32  ptr_id:u8  event:u8  x:f16  y:f16] × N
Footer:  crc32[4]
```
使用 miniz deflate 压缩。

### `app/` — 应用层

| 文件 | 说明 |
|------|------|
| `app_context.hpp` | `AppContext` — 持有所有长生命周期的 SDL 对象、渲染器及音频 |
| `app_args.hpp` | CLI 参数解析 |
| `game_loop.hpp` | `GameLoop::run_frame()` — 每帧的更新/渲染驱动 |
| `input_manager.hpp` | `InputManager` — 扁平 `PointerSlot[10]` 数组 + `KeyAction[10]` 键盘状态；支持鼠标、触摸与键盘输入；滑动检测；`to_judge_input()` 桥接到 `JudgeInputFrame` |

#### `GameLoop::run_frame()` 执行阶段

1. 计时与事件轮询
2. 暂停检查 → 冻结渲染
3. 推进时间（音频游标 / 无头模式 SIM_DT）
4. 退出条件检测
5. 引擎更新（自动演奏或手动判定 + hold 逻辑 + 特效）
6. 跳过中间模拟步骤（无头多步模式）
7. `build_frame()`
8. 渲染（拖影 / 运动模糊 / 普通路径）
9. HUD + 结算界面叠加层
10. 视频录制 / 截图

---

## 特效系统

### `HitFX`

由 `EffectManager::add_hitfx()` 在每个被判定的 Note 处生成。  
- 若 respack 包含精灵图集：在 Note 位置绘制动画帧序列
- 否则：绘制扩散圆环（`draw_circle_outline()`，14 段）
- 旋转动画：`rot += rot_speed × age`
- 非线性透明度淡出：`pow(1 − progress, 0.65)` — 初始快速闪光，逐渐消散

### `FlashFX`

与每个 `HitFX` 同时生成。短暂的扩散圆环（在 0.18s 内 `r: 12 → 70px`）。  
仅在没有 respack 精灵图集时渲染（与 Python 版本的回退行为一致）。

### `ParticleBurst`

由 `add_particle_burst()` 生成。  
每个粒子：旋转四边形，三次方大小曲线，减速度 `v*(9t/(8t+1))/2`。  
使用 `SDL_BLENDMODE_ADD` 渲染以实现发光效果；通过 `get_particles_inplace()` 复用缓冲区。

---

## 跨平台说明

### WASM

- SDL2 + Emscripten（`-DUSE_SDL3=OFF`）
- 以 `emscripten_set_main_loop_arg` 替代桌面端的 while 循环
- 当定义 `__EMSCRIPTEN__` 时，`TrailRenderer` 与 `MotionBlurRenderer` 被禁用（WebGL 不支持 `SDL_TEXTUREACCESS_TARGET`）
- 资源通过 `--preload-file respack.zip` 打包

### bgfx 后端

- 使用 `-DUSE_BGFX=ON --backend bgfx` 启用
- `BgfxExecutor` 将 `DrawList` 命令批量写入瞬态 VB/IB
- 预编译着色器位于 `shaders/compiled/`，支持 glsl/spirv/metal/dx11/essl

### 移动端

- 触摸事件 → `InputManager` 槽位 1–9
- CMake 移动端工具链文件位于 `cmake/`
