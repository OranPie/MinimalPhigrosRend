# 架构概览

> 🌐 [English](ARCHITECTURE.md)

本页面记录当前 C++ 模块布局、具体类型定义以及运行时工作流。

用户使用文档请参阅 [../../docs/CPP_RENDERER.zh.md](../../docs/CPP_RENDERER.zh.md)。  
各子系统详细说明请参见页面底部的内部文档链接。

---

## 目录结构

```text
cpp/
├── CMakeLists.txt
├── include/phigros/
│   ├── api/      Python 绑定所使用的原生 API 接口
│   ├── app/      CLI、窗口、输入、平台集成
│   ├── chart/    加载器、解析器、编译器、PHBC I/O
│   ├── config/   RenderConfig 加载/保存/默认值
│   ├── core/     核心类型、日志、Mod
│   ├── engine/   判定、自动游玩、Hold 逻辑、运动学、可见性
│   ├── hud/      HUD 状态
│   ├── io/       音频、回放、资源包、视频编码器
│   ├── math/     缓动、轨道、数学工具
│   └── render/   帧快照、渲染器、执行器、渲染目标
├── src/
│   ├── api/      PreparedChart / 自动游玩 / PHBC 实现
│   ├── app/      原生渲染器可执行入口
│   ├── chart/    解析器/加载器/编译器实现
│   ├── python/   Python 扩展模块胶水代码
│   ├── vendor/   第三方库翻译单元
│   └── main.cpp  无头/原生核心入口
├── tests/        原生测试和基准测试入口
├── vendor/       miniz、miniaudio、stb
├── scripts/      构建助手和启动器工具
├── shaders/      bgfx 着色器源码
├── mods/         内置 Mod 示例
├── web/          WASM Shell 资源
├── android/      Android 封装项目
└── ios/          iOS 封装项目
```

---

## 构建目标

`cpp/CMakeLists.txt` 中定义的主要目标：

| 目标 | 说明 |
|---|---|
| `phigros_core_lib` | 核心谱面/解析器/编译器库（无渲染依赖） |
| `phigros_core` | 基于 `phigros_core_lib` 的无头/原生 CLI 目标 |
| `phigros_python_api_lib` | Python 扩展所使用的原生 API 层 |
| `phigros_sdl_app` | 桌面端、Android 与 iOS 共用的 SDL 渲染器/播放器应用 |
| `phigros_render` | 旧 argv 渲染器/播放器应用（`BUILD_LEGACY_CLI=ON`） |
| `chart_scanner` | 谱面目录发现工具 |
| `test_easing`, `test_engine`, `test_parser`, `test_logger`, `test_zip_extract`, `verify_chart`, `bench` | 单元测试和基准测试入口 |

第三方支持库：`vendor_stb`、`vendor_miniz`、`vendor_miniaudio`。

---

## 核心类型（`core/types.hpp`）

### `NoteKind : int`

```cpp
enum class NoteKind : int { Tap = 1, Drag = 2, Hold = 3, Flick = 4 };
```

### `Note`（音符）

| 字段 | 类型 | 含义 |
|---|---|---|
| `nid`、`line_id`、`kind` | `int` | 标识符和类型（1–4） |
| `above`、`fake` | `bool` | 判定线哪侧；fake 音符不计入得分 |
| `t_hit`、`t_end` | `double` | 谱面时间中的打击/Hold 结尾时刻（秒） |
| `x_local_px`、`y_offset_px` | `double` | 判定线局部坐标 |
| `speed_mul`、`size_px`、`alpha01` | `double` | 单音符视觉修饰量 |
| `tint_rgb`、`tint_hitfx_rgb` | `math::RGB`、`optional<math::RGB>` | 音符着色 |
| `scroll_hit`、`scroll_end` | `double` | 关键时刻的滚动积分缓存值 |
| `visible_time` | `double` | 打击前多少秒出现（默认 999999 = 始终可见） |
| `t_enter` | `double` | 预计算的入屏时刻 |
| `mh` | `bool` | 同时打击（多手指）标志 |
| `hitsound_path` | `std::string` | RPE 自定义打击音效 |

### `Line`（判定线）

| 字段 | 类型 | 含义 |
|---|---|---|
| `lid` | `int` | 唯一判定线 ID |
| `pos_x`、`pos_y`、`rot`、`alpha` | `TrackFn`（= `std::function<double(double)>`） | 核心动画轨道 |
| `scroll_px` | `math::IntegralTrack` | 滚动距离积分 |
| `scroll_fn` | `TrackFn` | 编译滚动覆盖（`compile_chart()` 设置） |
| `color_rgb` | `math::RGB` | 静态兜底颜色 |
| `color` | `shared_ptr<math::PiecewiseColor>` | RPE 动态颜色轨道 |
| `compiled_color` | `ColorFn`（= `std::function<math::RGB(double)>`） | 编译颜色覆盖 |
| `scale_x`、`scale_y` | `shared_ptr<math::PiecewiseEased>` | RPE 缩放轨道 |
| `text` | `shared_ptr<math::PiecewiseText>` | RPE textEvents |
| `texture_path` | `std::string` | RPE 线条纹理 |
| `father`、`rotate_with_father` | `int`、`bool` | 父线层级 |
| `attach_ui` | `std::string` | RPE UI 元素绑定（隐藏该线） |
| `z_order`、`is_cover` | `int`、`bool` | RPE 绘制顺序控制 |
| `incline` | `shared_ptr<math::PiecewiseEased>` | RPE 透视倾斜（度） |
| `alpha_ctrl`、`pos_ctrl`、`size_ctrl`、`y_ctrl`、`skew_ctrl` | `vector<CtrlPoint>` | RPE 按滚动距离的音符属性修饰 |

### `CtrlPoint`（控制点）

```cpp
struct CtrlPoint {
    float x;      // 音符相对判定线的滚动距离（RPE y 单位）
    float value;  // 该距离处的属性值
    int   easing; // 1 = 线性
};
```

由 `eval_ctrl(pts, x, def)` 使用，在最近的两个控制点之间线性插值，边界外夹紧。

### `NoteState`（音符判定状态）

| 字段 | 类型 | 含义 |
|---|---|---|
| `judged`、`hit`、`miss` | `bool` | 判定结果标志 |
| `holding`、`released_early`、`hold_failed`、`hold_finalized` | `bool` | Hold 生命周期标志 |
| `judge_t`、`judge_delta_ms`、`judge_grade` | `double`、`double`、`string` | 判定结果 |
| `hold_grade` | `string` | Hold 头打击时的初始评级 |
| `release_t` | `double` | 提前释放的时刻 |

### `ChartData`（谱面数据）

| 字段 | 类型 | 含义 |
|---|---|---|
| `offset` | `double` | 音频偏移量（秒） |
| `lines` | `vector<Line>` | 所有判定线 |
| `notes` | `vector<Note>` | 所有音符，按 `t_hit` 排序 |
| `chart_end_t`、`playable_count` | `double`、`int` | 预计算摘要统计信息 |
| `is_compiled` | `bool` | 从 PHBC 加载时为 `true`（跳过 `precompute_t_enter`） |
| `early_notes` | `vector<size_t>` | `t_hit − t_enter > 15 s` 的音符索引（按 `t_enter` 排序） |
| `notes_by_enter` | `vector<size_t>` | 所有音符按 `t_enter` 排序的完整索引——用于 O(Δ) 逐帧裁剪 |
| `meta_song_path`、`meta_bg_path` | `string` | RPE META 资源路径 |

`ChartData::finalize()` 设置 `chart_end_t`、`playable_count` 和 `mh` 标志，解析后必须调用一次。

---

## 配置类型（`config/render_config.hpp`）

### `RenderConfig`

部分重要字段：

| 字段 | 类型 | 默认值 | 含义 |
|---|---|---|---|
| `window_w`、`window_h` | `int` | 1280、720 | 渲染分辨率 |
| `approach` | `double` | 3.0 | 音符入场时间（秒）；夹紧至 [0.1, 30] |
| `chart_speed` | `double` | 1.0 | 谱面速度倍率；夹紧至 [0.1, 20] |
| `playback_speed` | `optional<double>` | nullopt | 覆盖音频/时间速度；缺省时使用 `chart_speed` |
| `expand_factor` | `double` | 1.0 | 通道压缩因子（>1 时将世界坐标压向中心） |
| `note_scale_x`、`note_scale_y` | `double` | 2.5、1.0 | 音符尺寸倍率 |
| `note_alpha` | `double` | 1.0 | 全局音符透明度；夹紧至 [0, 1] |
| `font_size` | `double` | 1.0 | HUD 文字缩放；夹紧至 [0.5, 3] |
| `line_alpha_mode` | `LineAlphaMode` | `NegativeOnly` | 判定线 Alpha 如何调节音符 Alpha |
| `backend` | `string` | `"sdl3_bgfx"` | 渲染器后端字符串 |
| `autoplay` | `bool` | true | 启用 `SimulatePlayer` |
| `simulateplay` | `SimulatePlayConfig` | — | SimulatePlay 子配置 |
| `audio_offset_ms` | `double` | 0.0 | 正值 = 音符相对音频提前 |
| `trail_alpha`、`trail_frames`、`trail_decay` | `optional<double/int>` | — | 残影效果参数 |
| `motion_blur_samples`、`motion_blur_shutter` | `optional<int/double>` | — | 运动模糊参数 |

`LineAlphaMode` 取值：`Off`、`NegativeOnly`（默认——`alpha_raw < 0` 时暗化音符）、`Always`。

配置加载链：`load_config(path)` → `load_config_text(text)` → `load_config_json(json)`。  
序列化：`config_to_json(cfg)` → `save_config(path, cfg)`。

---

## 谱面加载工作流

```
谱面路径字符串
        │
        ▼
chart::resolve_chart_entry(path, preferred_difficulty="IN")
        │ 返回 optional<ChartEntry>
        │   ChartEntry { name, difficulty, chart_path,
        │                ChartAssets { music_path, illustration_path },
        │                source_type ("folder"|"zip"|"json") }
        ▼
按扩展名检测格式：
  .phbc  → chart::read_phbc(istream, password)
               → CompiledChartData → .to_chart_data() → ChartData
  .json  → nlohmann::json 解析
               → chart::parse_rpe(j, W, H, shift)    （RPE 2.x）
               → chart::parse_official(j, W, H)       （官方 .0 格式）
               → ChartData
  .pec   → chart::parse_pec(path, W, H)               （PEC 文本格式）
               → ChartData
  zip/pez → chart::extract_zip_file(zip_path, 压缩包内路径)
               → 字节 → 按上述格式解析
        │
        ▼
ChartData::finalize()                    — 设置 chart_end_t、playable_count、mh 标志
ChartData::build_early_notes_index()     — t_hit−t_enter > 15 s 的音符
ChartData::build_notes_by_enter_index()  — 完整 O(Δ) 裁剪索引（按 t_enter 排序）
        │
        ▼  （请求 --compile 时）
chart::compile_chart(ChartData, sample_rate=240 Hz)
        → CompiledChartData { offset, chart_end_t, playable_count,
                              sample_rate, t_start, sample_count,
                              vector<CompiledLine> { lid, color_rgb,
                                  vector<float> pos_x/y/rot/alpha/scroll,
                                  optional color_r/g/b 数组 },
                              vector<Note> }
chart::write_phbc(CompiledChartData, ostream, PhbcWriteOptions)
```

### PHBC 二进制格式（v2 文件头：52 字节）

```
[0-3]   魔数 "PHBC"
[4-5]   uint16_t version（1 或 2）
[6-7]   uint16_t flags — bit0=已压缩, bit1=LZMA, bit2=已加密, bits3-4=加密算法
[8-47]  double offset, chart_end_t; int32 playable_count, note_count,
        line_count; float sample_rate; double t_start; int32 sample_count
v2 元数据块（flags 置位时）：
  if compressed: uint32_t 未压缩大小
  if encrypted:  uint8_t salt[16], iv[16], tag[16]
每条判定线负载：
  int32 lid; uint8 color_r/g/b/_pad; int32 dyn_color
  [如有动态颜色] sample_count × float color_r/g/b
  sample_count × float pos_x/pos_y/rot/alpha/scroll
每个音符负载：
  int32 nid/line_id/kind; uint8 above/fake/mh/_pad
  double t_hit/t_end/t_enter/scroll_hit/scroll_end
  double x_local_px/y_offset_px; float speed_mul/size_px/alpha01
  uint8 tint_r/g/b/_pad
```

加密算法：`AES-256-GCM (0)`、`AES-256-CBC (1)`、`ChaCha20-Poly1305 (2)`、`XOR (3)`。  
压缩算法：`zlib (0)`、`LZMA (1)`。

---

## 运行时数据流

```
AppArgs（解析后的 CLI 参数）
  + config::RenderConfig（load_config 加载后叠加 CLI 覆盖）
        │
        ▼
AppContext::init(chart_path, chart_offset, respack_override,
                 bg_override, font_path, audio_override,
                 headless, W, H, cfg)
   ┌────────────────────────────────────────────────┐
   │  Window::init(W, H, title, headless, vsync,    │
   │               backend)                          │
   │  render::SpriteBatch::init(ren)                 │
   │  render::DrawList::reserve(2048)                │
   │  io::load_respack(ren, respack_path)            │
   │    → io::Respack { RespackConfig, 纹理,         │
   │                    hitsound_ogg[1..4] }          │
   │  BackgroundRenderer::load(…, bg_blur)           │
   │  NoteRenderer::init(W, H, scale_x, scale_y)    │
   │  HoldRenderer::init(…)                          │
   │  TrailRenderer::init(…, cfg)                    │
   │  MotionBlurRenderer::init(…, cfg)               │
   │  HudRenderer::init(ren, font, W, H, …)         │
   │  io::AudioSystem::init() + load_bgm(path, off) │
   │  io::AudioSystem::load_hitsound(kind, ogg)     │
   └────────────────────────────────────────────────┘
        │
        ▼
谱面加载（参见上方谱面加载工作流）
        │
        ▼
GameLoop::GameLoop(ctx, args, cfg, chart, playable_notes, chart_end)
   ┌────────────────────────────────────────────────┐
   │  vector<NoteState> states（每个音符一个）       │
   │  engine::Judge judge                            │
   │  engine::EffectManager effects                  │
   │  engine::SimulatePlayer autoplay（Conservative）│
   │  engine::ScriptPlayPlayer scriptplay            │
   │  engine::ManualJudge manual_judge               │
   │  io::ReplayWriter replay_writer                 │
   │  io::ReplayPlayer replay_player                 │
   │  io::RecordingSession recorder                  │
   └────────────────────────────────────────────────┘
        │
        ▼
GameLoop::run_frame() 循环 ────────────────────────────────────────────┐
  1. 从音频位置（或 SDL_GetTicks）推进谱面时间 t                        │
  2. engine::SimulatePlayer::step(t, notes, states, lines, judge, W, H)│
     或 engine::ManualJudge::update(t, …)（游玩模式）                  │
     或 engine::ScriptPlayPlayer::step(…)（脚本游玩）                  │
  3. engine::hold_logic_step(t, notes, states, judge)                  │
  4. engine::NoteManager::tick(t, notes, states, judge)（自动 miss）   │
  5. render::build_frame(t, chart, states, judge, cfg)                 │
     → engine::eval_line_state(line, t) × N_lines                      │
       → LineState { x, y, rot, alpha01, scroll, alpha_raw,            │
                     cos_rot, sin_rot }                                 │
     → engine::note_world_pos_cs(…) 对每个可见音符                     │
       → eval_ctrl(alpha/pos/size/y/skew, scroll_dist)                 │
       → NoteSnapshot { nid, kind, wx/wy, wx_tail/wy_tail,             │
                        alpha, line_rot, size_px, color,               │
                        is_hold, judged, miss, mh,                     │
                        holding, skew }                                 │
     → engine::compute_score(acc_sum, max_combo, playable_count)       │
       → ScoreResult { score, acc_ratio, combo_ratio }                 │
     → hud::update_hud(frame.hud, score, acc, combo, …)               │
     → FrameSnapshot { t, vector<LineSnapshot>, vector<NoteSnapshot>,  │
                       hud::HudState }                                  │
  6. render::Renderer::draw_frame(frame, ctx)                          │
     BackgroundRenderer → LineRenderer → NoteRenderer                  │
     → HoldRenderer → HitFXRenderer → TrailRenderer                   │
     → MotionBlurRenderer → HudRenderer                                │
  7. SDL_RenderPresent / bgfx::frame                                   │
  8. （可选）io::RecordingSession::capture_frame → 视频编码器           │
  9. （可选）按 --screenshot-fps 间隔截图写入磁盘                       │
  └────────────────────────────────────────────────────────────────── （循环）
```

---

## 运动学：`engine::eval_line_state` 和 `note_world_pos_cs`

**`eval_line_state(line, t) → LineState`**：
- 求值 `line.pos_x(t)`、`line.pos_y(t)`、`line.rot(t)`、`line.alpha(t)`。
- 滚动：若已编译使用 `line.scroll_fn(t)`，否则使用 `line.scroll_px.integral(t)`。
- 预计算 `cos_rot`、`sin_rot`，平摊该线所有音符的三角运算。
- 尊重 `force_line_alpha01` 和 `force_line_alpha01_by_lid` 覆盖。

**`note_world_pos_cs(line_x, line_y, cos_r, sin_r, scroll_now, note, scroll_target, for_tail, flow_mul, speed_mul_affects_travel, hold_keep_head) → Vec2`**：
- 切线方向 `(cos_r, sin_r)`，法线方向 `(-sin_r, cos_r)`。
- `dy = (scroll_target − scroll_now) × flow_mul`
- Hold 尾：`dy` 乘以 `note.speed_mul`；非 Hold 且 `speed_mul_affects_travel` 时同样缩放。
- `hold_keep_head`：夹紧 `dy ≥ 0`，防止头部穿越判定线。
- 最终位置：`{line_x + cos_r × x_local + (−sin_r) × y_local, line_y + sin_r × x_local + cos_r × y_local}`，其中 `y_local = sgn × dy × mult + note.y_offset_px`。

---

## 得分与判定（`engine/judge.hpp`）

### 判定窗口（距理想打击时刻，单位：秒）

| 评级 | 窗口 |
|---|---|
| PERFECT | ≤ 0.045 s |
| GOOD | ≤ 0.090 s |
| BAD | ≤ 0.150 s |
| MISS | > 0.150 s |

### 得分公式

```
acc_ratio   = acc_sum / playable_count        （PERFECT=1.0，GOOD=0.6，其余=0）
combo_ratio = max_combo / playable_count
score       = int(acc_ratio × 900000 + combo_ratio × 100000)   → 满分 1000000
```

### Hold 生命周期

`Judge::start_hold(ns, t)` → 评级头部打击时机，设置 `ns.hit`、`ns.holding`、`ns.hold_grade`。  
`Judge::finalize_hold(ns)` → 在 `t ≥ t_end` 或提前释放时调用；将评级提交到 `acc_sum`。  
`ns.hold_failed` 由 hold 逻辑在提前释放时置位（`release_t < t_end × hold_tail_tol`）。

---

## SimulatePlayer（`engine/simulateplay.hpp`）

三种模拟模式：

| 模式 | 时机抖动幅度 |
|---|---|
| `Conservative` | amp × 0.6 |
| `Aggressive` | amp × 1.0 |
| `Extreme` | amp × 1.6 |

主要数据类型：
- `PointerState` — 每根手指的位置、目标、`holding_note`、flick/fade 计时器。
- `NotePlan` — 预调度的判定计划：`judge_t`、世界坐标 `(x, y)`、`pointer_idx`。
- `SimPointerVisual` — 导出给渲染器：位置、`fade_alpha`、轨迹采样。
- `SimHitEvent` — 每次打击时发出：`note_idx`、`judge_t`、`delta_ms`、评级。

`SimulatePlayer::step(t, notes, states, lines, judge, W, H)` 执行步骤：
1. `release_finished_taps(t)` — 清除已超时的点击指针。
2. `release_completed_holds(t, …)` — 终结已过 `t_end` 的 Hold。
3. `plan_note(i, n, …, t)` — 在 `lookahead_s(kind)`（50–120 ms）内为即将到来的音符分配空闲指针并调度计划。
4. 触发就绪计划：调用 `judge.start_hold` 或 `judge.try_hit`。
5. `update_pointer_motion(t)` — 线性插值指针朝目标移动（人性化时最大速度 2600 px/s）。
6. `record_visual_trails(t)` — 追加轨迹采样，按 `trail_seconds_` 修剪。

---

## 帧快照与裁剪

`render::build_frame(t, chart, states, judge, cfg) → FrameSnapshot`：

1. **判定线阶段**：对所有判定线求值 `eval_line_state`；在栈上存储最多 256 条线的状态（更大谱面降级为堆）。按 `z_order` 稳定排序（全部相同时快速跳过）。
2. **音符裁剪**——四层过滤：
   - `visible_time` 过滤（`t < t_hit − visible_time` → 跳过）。
   - `t_enter` / `t_end` 过滤（可由 `no_cull_enter_time` 禁用，默认禁用）。
   - 负滚动幽灵过滤（裁剪滚动已预跑超过 `scroll_hit` 但 `t < t_hit` 的音符）。
   - 带扩展变换的屏幕边界测试（可由 `no_cull_screen` 禁用）。
3. **水印优化**：线程局部 `EnterWM` 跟踪 `notes_by_enter` 中最低活跃索引，跳过已完全到期的音符。后退跳转重置为 0；正常前进播放每帧仅付出 O(Δ 音符)。
4. **控制事件**：对每个可见音符调用 `eval_ctrl(alpha_ctrl/pos_ctrl/size_ctrl/y_ctrl/skew_ctrl, scroll_dist_rpe)`。
5. **计数排序**：按 `(is_hold, kind)` 桶排序，最小化 `SdlExecutor` 中的纹理状态切换（最多 8 个桶，O(N)）。

结果类型：
- `LineSnapshot { lid, x, y, rot, cos_rot, sin_rot, alpha01, scroll, color, incline, is_cover, z_order, scale_x, scale_y, texture_path*, text }`
- `NoteSnapshot { nid, kind, wx, wy, wx_tail, wy_tail, alpha, line_rot, size_px, color, is_hold, judged, miss, mh, holding, draw_hold_head, hold_hit_failed, skew }`
- `FrameSnapshot { t, vector<LineSnapshot>, vector<NoteSnapshot>, hud::HudState }`

---

## 资源包（`io/respack.hpp`）

`Respack` 通过 `io::load_respack(SDL_Renderer*, zip_path)` 加载：
- 解析 `info.yml` → `RespackConfig { hitfx_cols/rows, hold_head/tail_h, hitfx_duration/scale/rotate/tinted, holdKeepHead, holdRepeat, holdCompact, colorPerfect/Good }`。
- 纹理：`click`、`drag`、`flick`、`hold`（及 `_mh` 多手指变体）、`hitfx_sheet`。
- 音效 OGG 字节：`hitsound_ogg[1..4]`（索引 3 = Hold，可选）。
- 兜底：ZIP 打开失败时创建纯色占位纹理，保证渲染始终可用。

`Respack::note_texture(kind, mh_flag)` — 选择正确纹理，`_mh` 不存在时回退到普通变体。

---

## AppContext 成员

```
AppContext {
  Window                window        — SDL 窗口 + 渲染器（或 bgfx 设备）
  render::SpriteBatch   batch         — 2D 四边形批处理器
  render::DrawList      draw_list     — 延迟命令列表（容量 2048）
  io::Respack           respack       — 音符纹理 + 音效
  BackgroundRenderer    bg            — 模糊/暗化背景图像
  LineRenderer          line_ren      — 判定线绘制；line_w=H×0.005，dot_r=H×0.007
  NoteRenderer          note_ren      — Tap/Drag/Flick 精灵
  HoldRenderer          hold_ren      — Hold 主体 + 头尾拼接
  HitFXRenderer         hitfx_ren     — 精灵表打击特效 + 粒子
  HudRenderer           hud_ren       — 分数、连击、准确率、进度条
  TrailRenderer         trail         — 帧回声残影（可选）
  MotionBlurRenderer    motion_blur   — 多重采样运动模糊（可选）
  InputManager          input         — 指针/键盘事件
  io::AudioSystem       audio         — miniaudio BGM + 打击音效池
  unordered_map<string, Texture> line_tex_cache  — RPE 判定线纹理缓存（支持 ZIP）
}
```

音频路径解析优先级：CLI `--audio` 覆盖 → RPE META `meta_song_path` → respack 内嵌 → `find_chart_audio(chart_dir)`（扫描 `music.ogg/mp3/wav`、`bgm.*`，最后尝试任意 `*.ogg/.mp3/.wav/.flac`）。

---

## 模块职责

| 模块 | 主要类型 / 函数 |
|---|---|
| `core/` | `Note`、`Line`、`NoteState`、`ChartData`、`TrackFn`、`ColorFn`、`CtrlPoint`、`eval_ctrl()` |
| `chart/` | `ChartEntry`、`ChartAssets`、`CompiledChartData`、`SampledTrack`、`parse_official/rpe/pec()`、`compile_chart()`、`write_phbc/read_phbc()`、`scan_charts_directory()`、`resolve_chart_entry()` |
| `math/` | `PiecewiseEased`、`PiecewiseColor`、`PiecewiseText`、`IntegralTrack`、缓动库 |
| `engine/` | `LineState`、`eval_line_state()`、`note_world_pos_cs()`、`Judge`、`SimulatePlayer`、`ManualJudge`、`ScriptPlayPlayer`、`NoteManager`、`EffectManager`、`HoldLogic` |
| `render/` | `FrameSnapshot`、`LineSnapshot`、`NoteSnapshot`、`build_frame()`、`SpriteBatch`、`DrawList`、各渲染器类、`Texture` |
| `config/` | `RenderConfig`、`LineAlphaMode`、`load_config/config_to_json/save_config()` |
| `api/` | `PreparedChart`、自动游玩辅助——Python 绑定的稳定原生接口层 |
| `app/` | `AppContext`、`AppArgs`、`GameLoop`、`Window`、`InputManager`、平台连接 |
| `io/` | `Respack`、`RespackConfig`、`AudioSystem`、`ReplayWriter/Player`、`RecordingSession`、`VideoEncoder` |

---

## 内部文档索引

- [INTERFACES.zh.md](INTERFACES.zh.md)
- [DATA_STRUCTURES.zh.md](DATA_STRUCTURES.zh.md)
- [MATH.zh.md](MATH.zh.md)
- [FORMAT.zh.md](FORMAT.zh.md)
- [KINEMATICS.zh.md](KINEMATICS.zh.md)
- [RENDER.zh.md](RENDER.zh.md)
- [CONFIG.zh.md](CONFIG.zh.md)
- [BUILD_AND_TEST.zh.md](BUILD_AND_TEST.zh.md)
- [CHART_LOADER.zh.md](CHART_LOADER.zh.md)
- [DEBUG_FLAGS.zh.md](DEBUG_FLAGS.zh.md)
