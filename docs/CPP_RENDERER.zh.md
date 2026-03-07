# C++ 渲染器 — 参考指南

> 🌐 [English](CPP_RENDERER.md)
C++ 渲染器（`phigros_render`）是用于 Phigros 谱面的高性能、支持无头模式的原生渲染器。
它与 Python 版 `phic_renderer` 无任何运行时依赖，支持桌面平台（SDL 2/3）和 WebAssembly（Emscripten）。

---

## 目录

1. [构建](#构建)
2. [CLI 参考](#cli-参考)
3. [配置文件参考](#配置文件参考)
4. [视觉效果](#视觉效果)
5. [谱面脚本模式](#谱面脚本模式)
6. [快捷键](#快捷键)
7. [配置示例](#配置示例)

---

## 构建

### 桌面端（SDL）

```bash
cd cpp
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc) phigros_render
```

可执行文件位于 `cpp/build/phigros_render`。

### WebAssembly（Emscripten）

```bash
cd cpp
mkdir -p build_wasm && cd build_wasm
emcmake cmake .. -DUSE_SDL3=OFF -DUSE_BGFX=OFF
emmake make -j$(nproc)
# 使用内置预览服务器运行：
python3 scripts/serve.py
```

### 运行测试

```bash
cd cpp/build
./test_easing
./test_engine
```

### 基准测试

```bash
./phigros_render chart.json --benchmark --benchmark-iterations 20
```

---

## CLI 参考

```
Usage: phigros_render <chart_path> [options]
```

`chart_path` 可作为第一个位置参数传入，也可通过任意支持的路径形式指定。

### 谱面格式

| 扩展名 | 格式 |
|--------|------|
| `.json`  | 官方谱面或 RPE 谱面 |
| `.pec`   | PEC 旧版谱面 |
| `.phbc`  | 预编译二进制谱面（加载最快） |

### 播放

| 参数 | 说明 |
|------|------|
| `--play` | 交互模式（支持鼠标/触摸输入） |
| `--score-only` | 无头引擎评分（最快，不显示窗口） |
| `--duration <sec>` | 运行 N 秒后自动退出 |
| `--audio-offset <ms>` | 音频延迟补偿（正值 = 提前音符） |
| `--width <px>` | 窗口宽度（覆盖配置文件设置） |
| `--height <px>` | 窗口高度（覆盖配置文件设置） |

### 资源

| 参数 | 说明 |
|------|------|
| `--config <path>` | 渲染配置 JSON/JSONC 文件 |
| `--respack <path>` | 资源包 ZIP（皮肤、打击音效） |
| `--bg <path>` | 背景图片 |
| `--font <path>` | TTF 字体文件 |
| `--audio <path>` | BGM 音频文件（覆盖谱面内嵌音频） |

### Mod

| 参数 | 说明 |
|------|------|
| `--mod <file.mod.json>` | 对谱面应用 mod（可重复，按顺序应用） |

mod 格式参考请见 `docs/ADVANCE_MODE_GUIDE.md`。

### 谱面脚本

| 参数 | 说明 |
|------|------|
| `--script <file.chartscript.json>` | 运行声明式谱面播放列表 |

完整的 chartscript DSL 参考请见 `docs/CHARTSCRIPT.md`。

### 编译

| 参数 | 说明 |
|------|------|
| `--compile <out.phbc>` | 将谱面编译为二进制文件后退出 |
| `--sample-rate <Hz>` | 编译时的采样率（默认：240） |

预编译可减少多次运行时的加载时间，推荐在播放列表中使用。

### 录制

| 参数 | 说明 |
|------|------|
| `--record <output.mp4>` | 录制视频（无头模式） |
| `--record-preset <name>` | `fast` \| `balanced` \| `quality` \| `archive` |
| `--record-codec <codec>` | `libx264` \| `libx265` \| `libvpx-vp9` |
| `--record-fps <fps>` | 录制帧率（默认：60） |
| `--record-resolution WxH` | 例如 `1920x1080` |
| `--record-start <sec>` | 从指定时间开始录制 |
| `--record-end <sec>` | 在指定时间停止录制 |

### 回放

| 参数 | 说明 |
|------|------|
| `--save-replay <file.rep>` | 保存 `--play` 模式下的回放 |
| `--play-replay <file.rep>` | 播放已保存的回放 |

### 分析 / 工具

| 参数 | 说明 |
|------|------|
| `--info` / `-i` | 打印谱面元数据（判定线/音符数量、偏移）后退出 |
| `--list-charts <dir>` | 在指定目录下扫描并列出所有谱面 |
| `--benchmark` | 运行引擎基准测试（隐含 `--score-only`） |
| `--benchmark-iterations N` | 基准测试运行次数（默认：10） |

### 其他

| 参数 | 说明 |
|------|------|
| `--headless` | 不显示窗口 |
| `--screenshot-dir <dir>` | 每 5 秒保存一张 PNG 截图到指定目录 |
| `--backend <name>` | 渲染后端（`sdl`、`sdl_hw`、`sdl_sw`） |
| `--version` / `-v` | 打印版本后退出 |
| `--help` / `-h` | 打印帮助信息后退出 |

---

## 配置文件参考

渲染器通过 `--config` 接受 JSON（或 JSONC——支持 `//` 注释的 JSON）配置文件。

### 完整示例

```jsonc
// phigros_render 配置文件（JSONC —— 支持 // 注释）
{
  "window": {
    "w": 1280,
    "h": 720
  },

  "render": {
    // 谱面时间
    "approach": 3.0,               // 铺面时间（秒，范围 0.1 – 30）
    "chart_speed": 1.0,            // 谱面速度倍率（0.1 – 20）

    // 音符视觉
    "expand": 1.0,                 // 横向轨道宽度系数
    "note_scale_x": 2.5,           // 音符宽度缩放
    "note_scale_y": 1.0,           // 音符高度缩放
    "note_flow_speed_multiplier": 1.0,
    "note_alpha": 1.0,             // 全局音符透明度 [0, 1]
    "note_outline": false,         // 是否绘制音符边框

    // 判定线透明度 → 音符透明度联动
    // "off" | "negative_only"（默认）| "always"
    "line_alpha_affects_notes": "negative_only",

    // 裁剪（跳过屏幕外对象——建议保持 true 以提升性能）
    "no_cull": false,
    "no_cull_screen": false,
    "no_cull_enter_time": true,
    "overrender": 1.0,             // 裁剪矩形扩展系数

    // 打击效果
    "show_hitfx": true,
    "show_particles": true,
    "particle_count": 8,           // 每次打击的粒子数（0 – 64）
    "hitfx_intensity": 1.0,        // 所有打击效果的透明度倍率 [0, 2]

    // ── 残影效果 ─────────────────────────────────────────────────────────
    "trail_alpha": 0.5,            // 残影不透明度 [0, 1]
    "trail_frames": 8,             // 环形缓冲区深度（残影数量）
    "trail_decay": 0.85,           // 每帧透明度乘数
    "trail_blur": 2,               // 模糊半径步数
    "trail_dim": 20,               // 每帧变暗量 [0, 255]
    "trail_blur_ramp": true,       // 启用模糊渐变（最老的残影模糊最多）
    "trail_blend": "alpha",        // "alpha" | "add"

    // 增强残影（v2 新增）
    "trail_blur_quality": 2,       // 模糊链降采样次数（1 – 4）
    "trail_chromatic": 1.2,        // 每帧色差偏移量（像素，0 = 关闭）
    "trail_decay_curve": "gaussian", // "exponential"（默认）| "gaussian"
    "trail_glow": 0.3,             // 每帧残影的叠加辉光强度（0 = 关闭）

    // ── 运动模糊 ─────────────────────────────────────────────────────────
    "motion_blur_samples": 4,      // 累积采样数（2 – 16）
    "motion_blur_shutter": 0.5,    // 快门角度分数（0.1 – 1.0）
    "motion_blur_curve": "gaussian" // "uniform"（默认）| "gaussian"
  },

  "assets": {
    "respack": "./respack.zip",
    "bg": null,                    // 背景图片路径
    "bg_blur": 10,                 // 背景模糊强度（降采样系数）
    "bg_dim": 120                  // 背景遮罩亮度 [0, 255]
  },

  "gameplay": {
    "autoplay": true,              // 自动游玩（无需输入）
    "hold_tail_tol": 0.8,          // 长按提前松开容忍度 [0, 1]
    "hold_fx_interval_ms": 200,    // 长按打击效果触发间隔（毫秒）
    "audio_offset_ms": 0.0         // 正值 = 音符相对音频提前
  },

  "rpe": {
    "rpe_easing_shift": 0          // RPE 谱面的缓动索引偏移
  },

  "debug": {
    "basic_debug": false           // 显示 FPS 和音符数量叠加层
  }
}
```

### 字段速查表

#### `window`

| 键 | 类型 | 默认值 | 说明 |
|----|------|--------|------|
| `w` | int | 1280 | 窗口宽度（像素） |
| `h` | int | 720 | 窗口高度（像素） |

#### `render`

| 键 | 类型 | 默认值 | 说明 |
|----|------|--------|------|
| `approach` | float | 3.0 | 铺面时间（秒） |
| `chart_speed` | float | 1.0 | 谱面速度倍率 |
| `expand` | float | 1.0 | 轨道宽度系数 |
| `note_scale_x` | float | 2.5 | 音符宽度缩放 |
| `note_scale_y` | float | 1.0 | 音符高度缩放 |
| `note_flow_speed_multiplier` | float | 1.0 | 音符流动速度 |
| `note_alpha` | float | 1.0 | 全局音符不透明度 |
| `note_outline` | bool | false | 是否绘制音符边框 |
| `line_alpha_affects_notes` | string | `"negative_only"` | `"off"` / `"negative_only"` / `"always"` |
| `no_cull` | bool | false | 禁用所有裁剪 |
| `overrender` | float | 1.0 | 裁剪矩形扩展系数 |
| `show_hitfx` | bool | true | 显示打击效果 |
| `show_particles` | bool | true | 显示粒子效果 |
| `particle_count` | int | 8 | 每次打击的粒子数 |
| `hitfx_intensity` | float | 1.0 | 打击效果亮度 |

#### 残影效果

| 键 | 类型 | 默认值 | 说明 |
|----|------|--------|------|
| `trail_alpha` | float | — | 残影不透明度 [0, 1] |
| `trail_frames` | int | — | 残影环形缓冲区深度 |
| `trail_decay` | float | — | 每帧透明度乘数 |
| `trail_blur` | int | — | 模糊半径步数 |
| `trail_dim` | int | — | 每帧变暗量 [0, 255] |
| `trail_blur_ramp` | bool | — | 最老的残影模糊最多 |
| `trail_blend` | string | — | `"alpha"` 或 `"add"` |
| `trail_blur_quality` | int | 2 | 降采样次数（1 – 4）；值越高模糊越柔和 |
| `trail_chromatic` | float | 0 | 每帧色差偏移量（像素，0 = 禁用） |
| `trail_decay_curve` | string | `"exponential"` | `"exponential"` 或 `"gaussian"` |
| `trail_glow` | float | 0 | 每帧残影叠加辉光强度（0 = 禁用） |

#### 运动模糊

| 键 | 类型 | 默认值 | 说明 |
|----|------|--------|------|
| `motion_blur_samples` | int | — | 累积采样数（2 – 16） |
| `motion_blur_shutter` | float | — | 快门角度分数（0.1 – 1.0） |
| `motion_blur_curve` | string | `"uniform"` | `"uniform"` 或 `"gaussian"`（中心加权） |

#### `assets`

| 键 | 类型 | 默认值 | 说明 |
|----|------|--------|------|
| `respack` | string | `"./respack.zip"` | 资源包 ZIP 路径 |
| `bg` | string | — | 背景图片 |
| `bg_blur` | int | 10 | 背景模糊强度（降采样系数） |
| `bg_dim` | int | 120 | 背景遮罩亮度 [0, 255] |

#### `gameplay`

| 键 | 类型 | 默认值 | 说明 |
|----|------|--------|------|
| `autoplay` | bool | true | 自动游玩模式 |
| `hold_tail_tol` | float | 0.8 | 长按提前松开容忍度 |
| `hold_fx_interval_ms` | int | 200 | 长按打击效果间隔（毫秒） |
| `audio_offset_ms` | float | 0.0 | 音频偏移补偿 |

---

## 视觉效果

### 残影效果

残影渲染器维护一个过去帧的环形缓冲区。每渲染一帧，旧残影以递减的不透明度叠加在当前帧后方。

**`trail_decay_curve`**
- `"exponential"`（默认）：每帧将透明度乘以 `trail_decay`，衰减较快。
- `"gaussian"`：以最新残影为中心的平滑钟形曲线衰减，呈现更柔和的电影质感。

**`trail_blur_quality`**  
对每个残影应用降采样 → 升采样模糊链。每次处理将分辨率减半再放大。
设置为 2–3 可在不影响性能的前提下获得柔和的扩散效果。
在 WebAssembly 构建中无效。

**`trail_chromatic`**  
每个残影在 R 和 B 通道上偏移 `trail_chromatic × 帧龄` 像素，产生彩色色散残影效果。
通常 0.5–2.0 的值即可明显察觉。

**`trail_glow`**  
每个残影以叠加混合模式再额外合成一次，强度为 `trail_glow`，
使运动物体周围产生类似泛光的辉光效果。

### 运动模糊

运动模糊渲染器根据快门曲线对多个子帧采样进行加权累积。

**`motion_blur_samples`**：值越高效果越平滑，但性能开销越大。4 是推荐默认值。

**`motion_blur_shutter`**：控制每帧参与模糊的时间窗口。
0.5 = 180° 快门角（电影感）；1.0 = 全帧曝光。

**`motion_blur_curve`**
- `"uniform"`：所有采样权重相等。
- `"gaussian"`：中心加权，中间帧影响更大，产生更柔和、不刺眼的模糊效果。

---

## 谱面脚本模式

完整的声明式播放列表 DSL 参考请见 **[CHARTSCRIPT.md](CHARTSCRIPT.md)**。

快速开始：

```bash
phigros_render --script my_playlist.chartscript.json
```

或从谱面目录生成播放列表：

```bash
python3 scripts/gen_chartscript.py --charts_dir charts/ --output my_playlist.chartscript.json
```

---

## 快捷键

以下快捷键在交互模式（`--play`）和默认自动游玩模式下均有效：

| 按键 | 操作 |
|------|------|
| `Space` | 暂停 / 继续 |
| `R` | 从头重新开始 |
| `Esc` | 退出 |

---

## 配置示例

### 展示（高质量，电影风格）

```jsonc
{
  "render": {
    "trail_alpha": 0.7,
    "trail_frames": 12,
    "trail_decay": 0.88,
    "trail_decay_curve": "gaussian",
    "trail_blur_quality": 3,
    "trail_chromatic": 1.5,
    "trail_glow": 0.35,
    "motion_blur_samples": 6,
    "motion_blur_shutter": 0.6,
    "motion_blur_curve": "gaussian",
    "show_particles": true,
    "particle_count": 12
  }
}
```

### 环境氛围（柔和，低干扰）

```jsonc
{
  "render": {
    "trail_alpha": 0.4,
    "trail_frames": 8,
    "trail_decay": 0.80,
    "trail_decay_curve": "gaussian",
    "trail_blur_quality": 2,
    "trail_glow": 0.15,
    "motion_blur_samples": 4,
    "motion_blur_shutter": 0.5
  }
}
```

### 对战（强烈，快节奏）

```jsonc
{
  "render": {
    "trail_alpha": 0.85,
    "trail_frames": 6,
    "trail_decay": 0.92,
    "trail_blend": "add",
    "trail_chromatic": 2.0,
    "trail_glow": 0.5,
    "motion_blur_samples": 8,
    "motion_blur_shutter": 0.8,
    "motion_blur_curve": "uniform",
    "hitfx_intensity": 1.5,
    "particle_count": 16
  }
}
```

### 极简（干净，无特效）

```jsonc
{
  "render": {
    "show_particles": false,
    "show_hitfx": false,
    "note_outline": false
  }
}
```
