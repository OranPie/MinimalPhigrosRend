# MinimalPhigrosRend / phic_renderer — CONFIG & USAGE (配置与用法)

## 中文（简体）

### 1. 快速开始

#### 1.1 直接运行（不使用配置文件）

```bash
python3 -m phic_renderer --input <谱面文件或谱面包>
```

- `--input` 支持：
  - 单个谱面文件（如 `chart.json` / `chart.pez` / `*.zip`）
  - 谱面文件夹（chart pack folder）

#### 1.2 使用配置文件（推荐）

```bash
python3 -m phic_renderer --input <谱面文件或谱面包> --config config.jsonc
```

- `--config` 使用 **配置 v2**（JSONC：允许注释）
- **命令行参数优先级高于配置文件**（同名参数如果在 CLI 指定，则不会被配置覆盖）

#### 1.3 从旧版配置迁移到新版配置

旧版配置（纯 JSON）通过 `--config_old` 加载：

```bash
python3 -m phic_renderer --input <谱面文件或谱面包> --config_old config.json
```

将当前参数导出为新版配置（v2 JSONC，带注释模板）：

```bash
python3 -m phic_renderer --input <谱面文件或谱面包> --config_old config.json --save_config config_v2.jsonc
```

### 2. 配置 v2（JSONC）格式说明

配置 v2 是一个 **JSON 对象**，但支持以下注释：

- 行注释：`// 注释`、`# 注释`
- 块注释：`/* 注释 */`

配置根对象常见结构：

- `version`: 固定为 `2`
- `window`: 窗口尺寸
- `render`: 渲染相关
- `audio`: 音频相关
- `assets`: 资源路径（背景/资源包等）
- `gameplay`: 游玩逻辑相关
- `ui`: UI（字体/语言/标题层）
- `rpe`: RPE 兼容相关
- `debug`: 调试开关
- `mods`: mod 配置（透传给 mods 系统）

### 3. 配置优先级

从高到低：

1. **命令行参数（CLI）**
2. `--config`（v2 JSONC）
3. `--config_old`（旧版 JSON）
4. 代码内置默认值

### 4. 常用参数（按分类）

#### 4.1 输入（Input）

- `--input <path>`
  - 谱面文件 / 谱面包 / 文件夹
- `--advance <path>`
  - 高级/混合输入模式（如果你在用 advance 配置）

#### 4.2 窗口（Window）

- `--w <int>`：窗口宽度
- `--h <int>`：窗口高度

#### 4.3 渲染（Render）

- `--backend pygame|moderngl|gl|opengl`
  - 渲染后端
- `--approach <float>`
  - 预绘制时间（秒），越大越提前绘制
- `--chart_speed <float>`
  - 谱面速度倍率（影响判定/播放进度）
- `--expand <float>`
  - 画面收缩/扩展因子（用于适配不同谱面）
- `--note_scale_x <float>`, `--note_scale_y <float>`
  - note 尺寸缩放
- `--note_flow_speed_multiplier <float>`
  - note 流速倍率（影响滚动）
- `--multicolor_lines`
  - 允许判定线使用彩色（RPE/官方相关）
- `--no_note_outline`
  - 关闭 note 描边
- `--line_alpha_affects_notes never|negative_only|always`
  - 判定线 alpha 影响 note 的策略

#### 4.4 资源（Assets）

- `--respack <path>`
  - respack zip 路径
- `--bg <path>`
  - 背景图
- `--bg_blur <int>`
  - 背景模糊强度（downscale factor）
- `--bg_dim <int>`
  - 背景遮罩黑色层强度（0..255）

#### 4.5 音频（Audio）

- `--audio_backend pygame|openal|al`
  - 音频后端
- `--bgm <path>`
  - BGM 文件
- `--bgm_volume <float>`
  - BGM 音量（0..1）
- `--hitsound_min_interval_ms <int>`
  - hitsound 最小触发间隔（防止过密）

#### 4.6 游玩（Gameplay）

- `--autoplay`
  - 自动游玩
- `--hold_fx_interval_ms <int>`
  - hold tick hitfx 的间隔（毫秒）
- `--hold_tail_tol <float>`
  - hold 早松容忍（0..1）
- `--start_time <float>` / `--end_time <float>`
  - 截取谱面片段（秒）

#### 4.7 UI

- `--font_path <path>`
  - 字体文件路径
- `--font_size_multiplier <float>`
  - 字体倍率
- `--no_title_overlay`
  - 关闭标题 overlay

#### 4.8 CUI / 语言

- `--lang zh-CN|en`
  - 控制台文案语言
- `--quiet`
  - 关闭启动摘要输出
- `--no_color`
  - 关闭 ANSI 彩色

对应配置 v2：

```jsonc
{
  "ui": {
    "lang": "zh-CN"
  }
}
```

### 5. 配置 v2 示例（带注释）

```jsonc
// MinimalPhigrosRend 配置 v2（支持注释的 JSON）
{
  "version": 2,

  "window": {
    "w": 1280,
    "h": 720
  },

  "ui": {
    "lang": "zh-CN",
    "font_path": null,
    "font_size_multiplier": 1.5,
    "no_title_overlay": false
  },

  "render": {
    "backend": "pygame",
    "approach": 3.0,
    "chart_speed": 1.0,
    "expand": 1.0,

    "note_scale_x": 1.0,
    "note_scale_y": 1.0,
    "note_flow_speed_multiplier": 1.0,

    "multicolor_lines": false,
    "no_note_outline": false,
    "line_alpha_affects_notes": "negative_only",

    "overrender": 2.0,
    "trail_alpha": 0.0,
    "trail_blur": 0,
    "trail_dim": 0,

    "hitfx_scale_mul": 1.0
  },

  "audio": {
    "audio_backend": "pygame",
    "bgm": null,
    "bgm_volume": 0.8,
    "hitsound_min_interval_ms": 30
  },

  "assets": {
    "respack": "./respack.zip",
    "bg": null,
    "bg_blur": 10,
    "bg_dim": 120
  },

  "gameplay": {
    "autoplay": false,
    "hold_fx_interval_ms": 200,
    "hold_tail_tol": 0.8,
    "start_time": null,
    "end_time": null
  },

  "rpe": {
    "rpe_easing_shift": 0
  },

  "debug": {
    "debug_line_label": false,
    "debug_line_stats": false,
    "debug_judge_windows": false,
    "debug_note_info": false,
    "debug_particles": false
  }
}
```

### 6. 常见问题（FAQ）

#### 6.1 为什么我的配置里写了值，但运行时没生效？

因为命令行参数优先级更高。如果你在命令行里写了同名参数（例如 `--w 1920`），配置里的 `window.w` 会被忽略。

#### 6.2 如何确认实际运行使用了什么参数？

默认会打印启动摘要（可用 `--quiet` 关闭）。

---

## English

### 1. Quick Start

#### 1.1 Run without a config file

```bash
python3 -m phic_renderer --input <chart_or_pack>
```

- `--input` accepts:
  - a single chart file (e.g. `chart.json` / `chart.pez` / `*.zip`)
  - a chart folder (chart pack directory)

#### 1.2 Run with a config file (recommended)

```bash
python3 -m phic_renderer --input <chart_or_pack> --config config.jsonc
```

- `--config` uses **config v2** (JSONC: JSON with comments)
- **CLI args override config values**.

#### 1.3 Migrate legacy config to v2

Legacy config (plain JSON):

```bash
python3 -m phic_renderer --input <chart_or_pack> --config_old config.json
```

Export current settings as config v2 (JSONC with a commented template header):

```bash
python3 -m phic_renderer --input <chart_or_pack> --config_old config.json --save_config config_v2.jsonc
```

### 2. Config v2 (JSONC) format

Config v2 is a JSON object with comment support:

- Line comments: `// ...`, `# ...`
- Block comments: `/* ... */`

Top-level structure:

- `version`: always `2`
- `window`: window size
- `render`: rendering settings
- `audio`: audio settings
- `assets`: resource paths
- `gameplay`: gameplay/judging related settings
- `ui`: UI settings (font/language/title overlay)
- `rpe`: RPE compatibility settings
- `debug`: debug flags
- `mods`: mods object (passed through)

### 3. Precedence (priority)

From highest to lowest:

1. **CLI arguments**
2. `--config` (v2 JSONC)
3. `--config_old` (legacy JSON)
4. built-in defaults

### 4. Common options

#### 4.1 Input

- `--input <path>`: chart file / chart pack / folder
- `--advance <path>`: advanced input mode

#### 4.2 Window

- `--w <int>` / `--h <int>`

#### 4.3 Render

- `--backend pygame|moderngl|gl|opengl`
- `--approach <float>`
- `--chart_speed <float>`
- `--expand <float>`
- `--note_scale_x <float>` / `--note_scale_y <float>`
- `--note_flow_speed_multiplier <float>`
- `--multicolor_lines`
- `--no_note_outline`
- `--line_alpha_affects_notes never|negative_only|always`

#### 4.4 Assets

- `--respack <path>`
- `--bg <path>`
- `--bg_blur <int>`
- `--bg_dim <int>`

#### 4.5 Audio

- `--audio_backend pygame|openal|al`
- `--bgm <path>`
- `--bgm_volume <float>`
- `--hitsound_min_interval_ms <int>`

#### 4.6 Gameplay

- `--autoplay`
- `--hold_fx_interval_ms <int>`
- `--hold_tail_tol <float>`
- `--start_time <float>` / `--end_time <float>`

#### 4.7 UI

- `--font_path <path>`
- `--font_size_multiplier <float>`
- `--no_title_overlay`

#### 4.8 CUI / Language

- `--lang zh-CN|en`
- `--quiet`
- `--no_color`

Config v2:

```jsonc
{
  "ui": {
    "lang": "en"
  }
}
```

### 5. Example config v2 (JSONC)

See the Chinese example above; the structure is identical.

### 6. FAQ

#### 6.1 Why does my config value not take effect?

Because CLI arguments have higher priority. If you pass a CLI arg with the same key, it overrides the config.

#### 6.2 How do I know what values are actually used?

The program prints a startup summary by default (disable with `--quiet`).
