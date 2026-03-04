# ChartScript — 声明式谱面播放列表 DSL

> 🌐 [English](CHARTSCRIPT.md)
ChartScript 是内置于 `phigros_render` 中的播放列表脚本系统。
它以统一的、基于 JSON 的 DSL 取代了 Python 版 `advance.json` / `gen_advance_from_charts.py` 工作流，
表达能力更强、类型更完整，并完全运行于 C++ 渲染器内部。

---

## 目录

1. [快速开始](#快速开始)
2. [顶层字段](#顶层字段)
3. [条目](#条目)
4. [片段](#片段)
5. [音符窗口](#音符窗口)
6. [on_complete — 播放结束动作](#on_complete--播放结束动作)
7. [配置覆盖](#配置覆盖)
8. [内联 Mod](#内联-mod)
9. [分组](#分组)
10. [变量](#变量)
11. [预设](#预设)
12. [过滤器](#过滤器)
13. [全局过滤器](#全局过滤器)
14. [自动发现](#自动发现)
15. [恢复状态](#恢复状态)
16. [Python 生成器 — gen_chartscript.py](#python-生成器--gen_chartscriptpy)
17. [完整示例](#完整示例)

---

## 快速开始

```bash
phigros_render --script my_playlist.chartscript.json
```

最简播放列表，按顺序播放两张谱面：

```jsonc
{
  "version": 2,
  "name": "My Playlist",
  "mode": "sequence",
  "items": [
    { "input": "charts/song_a/AT.json", "name": "Song A" },
    { "input": "charts/song_b/IN.json", "name": "Song B", "end": 60.0 }
  ]
}
```

---

## 顶层字段

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `version` | int | 2 | Schema 版本（请使用 `2`） |
| `name` | string | — | 人类可读的播放列表名称 |
| `mode` | string | `"sequence"` | `"sequence"` / `"shuffle"` / `"loop"` |
| `shuffle_seed` | int | 0 | 随机洗牌种子（0 = 每次运行时随机） |
| `repeat` | int | 1 | 全部遍历次数，达到后停止（0 = 无限循环） |
| `discover_limit` | int | -1 | 限制 `discover` 发现的条目总数（-1 = 不限制） |
| `resume_file` | string | — | 保存/恢复播放游标位置的文件路径 |
| `transition` | object | none | 谱面间过渡效果 |
| `defaults` | object | — | 应用于每个条目的配置（可被条目级配置覆盖） |
| `global_filter` | object | — | 在发现后应用于所有条目的过滤器 |
| `variables` | object | — | 用于 `$name` 替换的变量映射 |
| `presets` | object | — | 命名的配置快捷方式 |
| `groups` | object | — | 命名的共享配置+mod 块 |
| `discover` | object | — | 从目录自动发现谱面 |
| `items` | array | — | 显式条目列表 |

### 播放模式

| 模式 | 说明 |
|------|------|
| `"sequence"` | 按列表顺序播放，每次遍历播放一遍 |
| `"shuffle"` | 每次遍历按加权随机顺序播放（每次 repeat 重新洗牌） |
| `"loop"` | 与 sequence 相同，但无限循环直到按 `Esc` |

### 过渡效果

```jsonc
"transition": {
  "type": "fade",       // "none" | "fade" | "crossfade"
  "duration": 0.5       // 秒
}
```

---

## 条目

`"items"` 中的每个条目描述一张要播放的谱面。

```jsonc
{
  "input":    "charts/song/AT.json",  // 谱面文件路径（必填）
  "name":     "Song Title",           // 显示名称
  "level":    "AT",                   // 难度标签（用于过滤器）
  "bgm":      "song.ogg",             // 覆盖 BGM（可选）
  "bg":       "bg.jpg",               // 覆盖背景图（可选）

  "start":    0.0,                    // 谱面中的开始时间（秒）
  "end":      60.0,                   // 结束时间（-1 = 谱面末尾）
  "start_at": 0.0,                    // 在主时间线上的偏移量

  "notes_window": 200,                // 自动以前 200 个音符计算结束时间
  "tail_time":    0.8,                // 窗口内最后一个音符后的秒数

  "segments": [ ... ],                // 多个时间窗口（覆盖 start/end）

  "weight":   2,                      // 随机模式下的相对概率权重
  "group":    "hype",                 // 从命名分组继承配置
  "tags":     ["boss", "fast"],       // 供过滤器和分组使用的标签

  "config":   { "chart_speed": 1.5 },// 条目级配置覆盖
  "mods":     [ ... ],                // 内联 mod
  "mod_file": "my.mod.json",          // 外部 mod 文件

  "filter":   { "min_notes": 300 },   // 若过滤器不匹配则跳过此条目

  "on_complete": { ... }              // 此条目结束后执行的动作
}
```

### 字段说明

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `input` | string | 必填 | 谱面文件路径 |
| `name` | string | — | 显示名称 |
| `level` | string | — | 难度标签（`AT`、`IN`、`HD`、`EZ` 等） |
| `bgm` | string | — | 覆盖 BGM 路径 |
| `bg` | string | — | 覆盖背景图片 |
| `start` | float | 0.0 | 开始时间（谱面内秒数） |
| `end` | float | -1 | 结束时间（-1 = 谱面末尾 + 2 秒填充） |
| `start_at` | float | 0.0 | 在播放列表主时间线上的偏移量 |
| `notes_window` | int | -1 | 自动以前 N 个音符计算结束时间 |
| `tail_time` | float | 0.5 | 窗口内最后一个音符后的填充秒数 |
| `segments` | array | — | 多个时间窗口（覆盖 `start`/`end`） |
| `weight` | int | 1 | 随机权重（越大被选中的概率越高） |
| `group` | string | — | 从该命名分组继承配置+mod |
| `tags` | array | — | 用于过滤器匹配的字符串标签 |
| `config` | object | — | 条目级渲染配置覆盖 |
| `mods` | array | — | 内联 mod 操作 |
| `mod_file` | string | — | 要应用的外部 `.mod.json` 文件 |
| `filter` | object | — | 若过滤器不匹配则跳过此条目 |
| `on_complete` | object | — | 此条目结束后执行的动作 |

---

## 片段

当您希望从同一张谱面播放多个不连续的时间窗口而无需重复条目时，可使用 `segments`。
当 `segments` 非空时，条目级别的 `start`/`end` 将被忽略。

```jsonc
"segments": [
  { "start": 0.0,  "end": 20.0 },
  { "start": 60.0, "end": 80.0, "notes_window": 50, "tail_time": 1.0 }
]
```

每个片段对象的字段：

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `start` | float | 0.0 | 片段开始时间（秒） |
| `end` | float | -1 | 片段结束时间（-1 = 谱面末尾） |
| `notes_window` | int | -1 | 在片段内自动以前 N 个音符计算结束时间 |
| `tail_time` | float | 0.5 | 窗口内最后一个音符后的秒数 |

---

## 音符窗口

`notes_window` 允许您指定包含的音符数量，而非硬性设定结束时间。
渲染器会对谱面中所有击打时间排序，并自动将结束时间设为：

```
end = time_of_nth_note + tail_time
```

这与 Python 版 `gen_advance_from_charts.py --notes_per_chart` 的行为一致。

```jsonc
{
  "input": "charts/hard_chart/AT.json",
  "notes_window": 300,    // 播放至第 300 个音符（含）
  "tail_time": 1.0        // + 1 秒缓冲时间
}
```

---

## on_complete — 播放结束动作

`on_complete` 控制一个条目（或其所有片段）播放完毕后发生的行为。

```jsonc
"on_complete": {
  "action": "next"         // 默认：前进至下一个条目
}
```

### 动作

| 动作 | 说明 |
|------|------|
| `"next"` | 前进至列表中的下一个条目 |
| `"repeat"` | 立即重新播放当前条目 |
| `"loop"` | 跳回第 0 个条目 |
| `"stop"` | 结束播放列表 |
| `"goto"` | 跳转至指定条目索引（从 0 开始） |

对于 `"goto"`，还需设置 `"goto": <index>`：

```jsonc
"on_complete": { "action": "goto", "goto": 3 }
```

### 基于分数的条件分支

若设置了 `min_score`，则根据分数是否达到阈值来选择执行的动作：

```jsonc
"on_complete": {
  "min_score":   900000,   // 若分数 >= 900000：
  "action":      "next",   //   前进至下一个
  "else_action": "repeat"  // 否则：重试此谱面
}
```

这让您可以构建技术关卡式播放列表——玩家必须通过一张谱面才能继续。

---

## 配置覆盖

条目级 `config` 对象仅对该条目覆盖全局渲染配置。
字段名称与 JSONC 配置文件中的相同（位于 `"render"`、`"assets"` 等节点下），
但在 config 对象的顶层以扁平方式书写。

```jsonc
"config": {
  "chart_speed": 1.5,
  "trail_alpha": 0.7,
  "trail_chromatic": 2.0,
  "trail_glow": 0.4,
  "motion_blur_samples": 6,
  "motion_blur_shutter": 0.6,
  "motion_blur_curve": "gaussian",
  "bg_dim": 80
}
```

您也可以按名称引用预设（参见[预设](#预设)）：

```jsonc
"config": { "preset": "vibrant", "chart_speed": 1.8 }
```

条目级值覆盖预设值，预设值覆盖 `defaults`。

---

## 内联 Mod

`mods` 数组接受与 `.mod.json` 文件相同的 mod 操作，以内联方式书写。

```jsonc
"mods": [
  { "type": "colorize", "mode": "hue", "hue_s": 0.9, "hue_v": 0.85 },
  { "type": "mirror" },
  { "type": "speed",    "mul": 1.2 }
]
```

支持的 mod 类型：`mirror`、`colorize`、`speed`、`opacity`、`wave`、`shuffle`、
`note_filter`、`flip_timing`、`scale`。

也可以从文件加载 mod：

```jsonc
"mod_file": "mods/chromatic.mod.json"
```

若同时设置了 `mods` 和 `mod_file`，则先应用 `mod_file`，再应用内联 `mods`。

---

## 分组

分组允许您定义一个命名的配置+mod 块，并从多个条目中引用它，
从而避免在许多条目中重复相同的视觉预设。

```jsonc
"groups": {
  "hype": {
    "config": {
      "trail_alpha": 0.8,
      "trail_chromatic": 2.0,
      "trail_glow": 0.4,
      "motion_blur_samples": 6
    },
    "mods": [
      { "type": "colorize", "mode": "hue" }
    ]
  },
  "chill": {
    "config": { "trail_alpha": 0.3, "trail_decay_curve": "gaussian" }
  }
}
```

条目通过名称引用分组：

```jsonc
{ "input": "charts/boss.json", "group": "hype" }
```

**合并优先级**（越靠前优先级越高）：
1. 条目级 `config` / `mods`
2. 分组 `config` / `mods`
3. 脚本 `defaults`

分组 mod 会插入在条目级内联 mod 之前。

---

## 变量

变量允许您定义一次值，然后在脚本中任意字符串处以 `"$name"` 形式引用。
这有助于将常用的调节值集中在一处管理。

```jsonc
"variables": {
  "spd":  1.4,
  "glow": 0.35,
  "bg_d": 80
},

"presets": {
  "vibrant": {
    "trail_glow":   "$glow",
    "chart_speed":  "$spd",
    "bg_dim":       "$bg_d"
  }
}
```

变量替换会递归应用于 `presets`、`defaults`、`groups` 以及条目级 `config` 中的所有字符串值。
非字符串值（数字、布尔值）原样传递；只有以 `$` 开头的字符串才会被替换。

---

## 预设

预设是命名的配置模板，条目可通过 `"preset": "name"` 来扩展它。

```jsonc
"presets": {
  "vibrant": {
    "trail_alpha":        0.7,
    "trail_decay_curve":  "gaussian",
    "trail_blur_quality": 3,
    "trail_chromatic":    1.5,
    "trail_glow":         0.35,
    "motion_blur_samples": 4,
    "motion_blur_curve":  "gaussian"
  },
  "minimal": {
    "trail_alpha":  0.0,
    "show_hitfx":   false,
    "show_particles": false
  }
}
```

在条目的 config 中使用：

```jsonc
"config": { "preset": "vibrant", "chart_speed": 1.8 }
```

条目级键覆盖预设键，预设键覆盖 `defaults`。

---

## 过滤器

条目上的 `filter` 会在过滤器不匹配时跳过该条目。
这在与 `discover` 结合使用时最为实用，可有选择地排除谱面。

```jsonc
"filter": {
  "levels":       ["AT", "IN"],   // item.level 必须在此列表中（不区分大小写）
  "min_notes":    100,            // 总音符数 < min_notes 时跳过
  "max_notes":    2000,           // 总音符数 > max_notes 时跳过
  "name_contains": "CHAOS",       // 对 item.name 进行子串匹配（不区分大小写）
  "tags_any":     ["featured"]    // 条目必须拥有其中至少一个标签（OR 逻辑）
}
```

所有条件均为 AND 组合（全部满足才通过）。空字段或省略的字段将被忽略。

---

## 全局过滤器

`global_filter` 在发现完成、显式 `items` 列表构建完毕后，应用于播放列表中的**每一个**条目。
不满足全局过滤器的条目将被移除。

```jsonc
"global_filter": {
  "levels": ["AT", "IN"],
  "min_notes": 50
}
```

当您希望对 `discover` 生成的所有内容统一过滤，而不必为每个发现的条目单独添加 `filter` 时，此功能非常实用。

---

## 自动发现

与其手动列出条目，`discover` 可在运行时扫描目录以查找谱面文件。

```jsonc
"discover": {
  "directory": "charts/",        // 要扫描的根目录
  "levels":    ["AT", "IN"],     // 仅包含难度匹配的文件
  "recursive": true,             // 扫描子目录
  "sort_by":   "name",           // "name" | "notes" | "difficulty" | "random"
  "limit":     50                // 最多 50 个条目（-1 = 不限制）
}
```

已发现的条目会继承 `defaults` 配置和任何 `global_filter`。

顶层的 `discover_limit` 对所有发现调用提供额外的总数上限。

### 同时使用 discover 和 items

若同时存在 `discover` 和 `items`，已发现的条目将追加在显式条目列表之后。

---

## 恢复状态

当设置了 `resume_file` 时，渲染器会在每个条目播放完毕后保存当前游标位置（条目索引）。
下次运行时，播放将从上次中断处恢复。

```jsonc
"resume_file": ".playlist_resume.json"
```

该文件内容为 `{"cursor": N}`。删除它即可从头开始播放。

将 `resume_file` 设为空字符串（或省略）则禁用恢复功能。

---

## Python 生成器 — gen_chartscript.py

`scripts/gen_chartscript.py` 可从谱面目录生成 `.chartscript.json` 播放列表。

```bash
python3 scripts/gen_chartscript.py --charts_dir charts/ --output playlist.chartscript.json
```

### 参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--charts_dir DIR` | `charts/` | 要扫描的谱面根目录 |
| `--output FILE` | `playlist.chartscript.json` | 输出文件 |
| `--mode MODE` | `sequence` | `sequence` / `shuffle` / `loop` |
| `--preset PRESET` | `minimal` | 视觉预设：`ambient` / `showcase` / `battle` / `minimal` |
| `--levels LEVELS` | 全部 | 逗号分隔的难度过滤器，例如 `AT,IN` |
| `--notes_window N` | -1 | 每张谱面在 N 个音符处自动截断 |
| `--tail_time T` | 0.5 | 窗口内最后一个音符后的秒数 |
| `--order ORDER` | `name` | `name` / `notes` / `random` |
| `--seed SEED` | 0 | 随机种子（0 = 随机） |
| `--limit N` | -1 | 最多包含的谱面数 |
| `--repeat N` | 1 | 播放列表重复次数（0 = 无限） |
| `--resume` | 关闭 | 启用恢复状态（写入 `.resume.json`） |
| `--compat` | 关闭 | 同时写入旧版 `advance.json` |
| `--info` | 关闭 | 打印播放列表统计摘要 |

### 内置预设

| 预设 | 说明 |
|------|------|
| `ambient` | 柔和高斯轨迹、轻微光晕、低强度效果 |
| `showcase` | 高质量轨迹、色差偏移、光晕、高斯运动模糊 |
| `battle` | 叠加混合轨迹、强烈色差、高强度光晕、快速运动模糊 |
| `minimal` | 无特效（干净，速度最快） |

### 示例

```bash
# 对所有 AT 谱面进行随机播放，使用 showcase 视觉，每段 200 个音符
python3 scripts/gen_chartscript.py \
  --charts_dir charts/ --levels AT \
  --mode shuffle --preset showcase \
  --notes_window 200 --tail_time 1.0 \
  --output at_showcase.chartscript.json

# 无限循环播放所有谱面，使用 ambient 视觉
python3 scripts/gen_chartscript.py \
  --charts_dir charts/ --mode loop --repeat 0 \
  --preset ambient --output ambient_loop.chartscript.json
```

---

## 完整示例

```jsonc
// 展示所有 v2 功能的完整播放列表。
// 用法：phigros_render --script full_example.chartscript.json
{
  "version": 2,
  "name": "Showcase Playlist",
  "mode": "shuffle",
  "shuffle_seed": 0,
  "repeat": 0,
  "resume_file": ".resume.json",
  "discover_limit": 30,

  // ── 变量 ──────────────────────────────────────────────────────────────────
  "variables": {
    "spd":  1.4,
    "glow": 0.35,
    "chro": 1.8
  },

  // ── 预设 ──────────────────────────────────────────────────────────────────
  "presets": {
    "vibrant": {
      "chart_speed":        "$spd",
      "trail_alpha":        0.72,
      "trail_frames":       10,
      "trail_decay_curve":  "gaussian",
      "trail_blur_quality": 3,
      "trail_chromatic":    "$chro",
      "trail_glow":         "$glow",
      "motion_blur_samples": 4,
      "motion_blur_shutter": 0.55,
      "motion_blur_curve":  "gaussian"
    },
    "boss": {
      "trail_alpha":  0.9,
      "trail_blend":  "add",
      "trail_chromatic": 2.5,
      "trail_glow":   0.6,
      "hitfx_intensity": 1.4,
      "particle_count": 14
    }
  },

  // ── 分组 ──────────────────────────────────────────────────────────────────
  "groups": {
    "hype": {
      "config": { "preset": "vibrant" },
      "mods":   [{ "type": "colorize", "mode": "hue", "hue_s": 0.9 }]
    },
    "boss_fight": {
      "config": { "preset": "boss" },
      "mods":   [{ "type": "mirror" }, { "type": "speed", "mul": 1.1 }]
    }
  },

  // ── 全局过滤器 ────────────────────────────────────────────────────────────
  "global_filter": { "levels": ["AT", "IN"], "min_notes": 80 },

  // ── 默认值 ────────────────────────────────────────────────────────────────
  "defaults": {
    "chart_speed": 1.0,
    "trail_alpha": 0.5,
    "trail_frames": 8,
    "trail_decay_curve": "gaussian",
    "show_hitfx": true,
    "show_particles": true
  },

  // ── 过渡效果 ──────────────────────────────────────────────────────────────
  "transition": { "type": "fade", "duration": 0.4 },

  // ── 自动发现 ──────────────────────────────────────────────────────────────
  "discover": {
    "directory": "charts/",
    "recursive": true,
    "sort_by": "random"
  },

  // ── 显式条目（追加在发现的条目之前）──────────────────────────────────────
  "items": [
    // 教程式条目：播放前 100 个音符，通过前反复重试
    {
      "input": "charts/tutorial/EZ.json",
      "name": "Tutorial",
      "level": "EZ",
      "tags": ["intro"],
      "notes_window": 100,
      "tail_time": 1.5,
      "config": { "chart_speed": 0.9 },
      "on_complete": {
        "action":      "next",
        "min_score":   700000,
        "else_action": "repeat"
      }
    },

    // 多片段条目：播放同一张谱面的两段精华片段
    {
      "input": "charts/epic_song/AT.json",
      "name": "Epic Song (highlights)",
      "level": "AT",
      "group": "hype",
      "weight": 3,
      "tags": ["featured", "fast"],
      "segments": [
        { "start": 0.0,  "end": 20.0 },
        { "start": 75.0, "end": 95.0 }
      ],
      "on_complete": { "action": "next" }
    },

    // Boss 谱面：关卡解锁——分数 >= 950000 才能前进
    {
      "input": "charts/final_boss/IN.json",
      "name": "Final Boss",
      "level": "IN",
      "group": "boss_fight",
      "weight": 1,
      "tags": ["boss"],
      "on_complete": {
        "action":      "next",
        "min_score":   950000,
        "else_action": "repeat"
      }
    }
  ]
}
```
