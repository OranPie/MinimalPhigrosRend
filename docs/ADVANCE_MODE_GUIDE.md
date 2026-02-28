# Phigros Renderer Advance Mode 使用指南

Advance Mode 允许你将多个谱面组合播放，支持顺序播放（sequence）和多谱叠加（composite）两种模式。

## 基本用法

```bash
python phic_pygame_renderer.py --advance advance.json
```

## JSON 配置格式

### 1. Sequence 模式（顺序播放）

按顺序播放多个谱面片段，每个片段可以指定时间范围。

```json
{
  "mode": "sequence",
  "mix": false,
  "items": [
    {
      "input": "chart1.json",
      "start": 30.0,
      "end": 90.0,
      "start_at": 0.0,
      "time_offset": 0.0,
      "chart_speed": 1.0,
      "bgm": "music1.ogg",
      "bg": "bg1.jpg"
    },
    {
      "input": "chart2.json", 
      "start": 0.0,
      "end": 60.0,
      "start_at": 60.0,
      "time_offset": 0.0,
      "chart_speed": 1.2,
      "bgm": "music2.ogg",
      "bg": "bg2.jpg"
    }
  ]
}
```

#### 字段说明：
- `mode`: `"sequence"` 或 `"composite"`
- `mix`: BGM 混音模式（详见下文）
- `items`: 片段列表

**每个片段的字段：**
- `input`: 谱面文件路径（支持 .json 或压缩包 .zip/.pez）
- `start`: 谱面开始时间（秒）
- `end`: 谱面结束时间（秒，可选）
- `start_at`: 在主时间轴的开始播放时间（秒）
- `time_offset`: 时间偏移（秒），用于对齐
- `chart_speed`: 谱面速度倍率（默认 1.0）
- `bgm`: BGM 文件路径（可选，覆盖谱面包内的 BGM）
- `bg`: 背景图片路径（可选，覆盖谱面包内的背景）

### 2. Composite 模式（多谱叠加）

同时播放多个谱面轨道，所有音符叠加显示。

```json
{
  "mode": "composite",
  "mix": true,
  "main": 0,
  "tracks": [
    {
      "input": "chart1.json",
      "start_at": 0.0,
      "end_at": 120.0,
      "time_offset": 0.0,
      "chart_speed": 1.0,
      "bgm": "main.ogg"
    },
    {
      "input": "chart2.json",
      "start_at": 30.0,
      "end_at": 90.0,
      "time_offset": 0.0,
      "chart_speed": 0.8,
      "bgm": "overlay.ogg"
    }
  ]
}
```

#### 字段说明：
- `main`: 主轨道索引（用于确定主 BGM）
- `tracks`: 轨道列表

**每个轨道的字段：**
- `input`: 谱面文件路径
- `start_at`: 在主时间轴的开始时间
- `end_at`: 在主时间轴的结束时间（可选）
- `time_offset`: 时间偏移
- `chart_speed`: 谱面速度倍率
- `bgm`: 该轨道的 BGM

## BGM 混音模式

### mix = false（默认）
- 使用 `pygame.mixer.music` 单路播放
- Sequence 模式：按片段切换 BGM
- Composite 模式：只播放主轨道 BGM

### mix = true
- 尝试使用 `pygame.mixer.Sound` 多路混音
- 每个 BGM 在指定 `start_at` 时开始播放
- 如果加载失败，自动回退到 mix = false 模式

**限制：**
- Sound 无法 seek（快进/快退）
- 混音会占用更多音频通道

## 路径解析规则

1. 所有相对路径（包括谱面、BGM、背景、hitsound）默认相对于 `advance.json` 所在目录
2. 如果相对路径在 advance.json 目录不存在，会尝试在谱面所在目录查找

## 时间轴计算

### Sequence 模式
```
主时间轴: 0s ──────► 60s ──────► 120s
         │ chart1  │ chart2  │
         │ 30-90s  │ 0-60s   │
         │ speed 1 │ speed 1.2│
```

### Composite 模式
```
主时间轴: 0s ──────► 30s ──────► 90s ──────► 120s
         │ chart1  │ chart1+chart2 │ chart1  │
         │ 全程    │ 30-90s叠加    │ 全程    │
```

## 实用示例

### 1. 练习特定段落
```json
{
  "mode": "sequence",
  "items": [
    {
      "input": "difficult_chart.json",
      "start": 60.0,
      "end": 80.0,
      "start_at": 0.0,
      "chart_speed": 0.5
    }
  ]
}
```

### 2. 合并多个短谱
```json
{
  "mode": "sequence",
  "items": [
    {"input": "part1.json", "start_at": 0.0},
    {"input": "part2.json", "start_at": 60.0},
    {"input": "part3.json", "start_at": 120.0}
  ]
}
```

### 3. 双人合作谱
```json
{
  "mode": "composite",
  "mix": true,
  "tracks": [
    {
      "input": "player1.json",
      "start_at": 0.0,
      "chart_speed": 1.0
    },
    {
      "input": "player2.json", 
      "start_at": 0.0,
      "chart_speed": 1.0
    }
  ]
}
```

### 4. 变速练习
```json
{
  "mode": "sequence",
  "items": [
    {
      "input": "chart.json",
      "start": 0.0,
      "end": 60.0,
      "start_at": 0.0,
      "chart_speed": 0.8
    },
    {
      "input": "chart.json",
      "start": 0.0,
      "end": 60.0,
      "start_at": 60.0,
      "chart_speed": 1.0
    },
    {
      "input": "chart.json",
      "start": 0.0,
      "end": 60.0,
      "start_at": 120.0,
      "chart_speed": 1.2
    }
  ]
}
```

## MOD GUIDE（Mods 配置指南）

Renderer 支持通过 `mods` 对谱面进行“运行时重写”（不改原谱面文件），用于练习、视觉模式、强制属性、以及各种自定义花样。

### 1. mods 放在哪里

- **Advance Mode**：在 `advance.json` 顶层加入 `"mods"`。
- **普通模式 / 通用配置**：在 `--config config.json` 的 JSON 顶层加入 `"mods"`。

### 2. 合并与优先级

- 如果同时提供 `--config` 的 `mods` 与 `advance.json` 的 `mods`：
  - 两者会合并。
  - 同名字段以 `advance.json` 的 `mods` 为准（覆盖 `--config`）。

生效顺序（越靠后优先级越高）：

- `mods.force_line_alpha` / `mods.note_speed_mul_affects_travel`（全局开关）
- `mods.full_blue`（FullBlue Mode）
- `mods.rules` / `mods.note_rules`（按条件批量修改 notes）
- `mods.note_overrides`（全局强制 notes；会覆盖前面的规则结果）
- `mods.line_rules`（按条件批量修改 lines / 按线强制 alpha）

Line alpha 的最终取值优先级：

- `mods.line_rules[].set.force_alpha`（按 line id 强制，最高）
- `mods.force_line_alpha`（全局强制）
- 原谱面事件（官方 / RPE 自带 alpha 事件）

### 3. 通用约定

- **Note kind**：`1=tap`，`2=drag`，`3=hold`，`4=flick`
- **alpha 写法**：
  - 支持 `0..1`（例如 `0.5`）
  - 支持 `0..255`（例如 `128`）
- **side 写法**：
  - `"above"` / `"below"` / `"flip"`
  - 或直接用 `true/false`（等价于 `above`）

### 4. 全局开关（不绑定任何模式）

放在 `mods` 顶层：

- `force_line_alpha`：强制所有线 alpha（支持 `255` 或 `1.0`）
- `note_speed_mul_affects_travel`：让非 Hold 音符的移动距离也乘上 `speed_mul`

示例：

```json
{
  "mods": {
    "force_line_alpha": 255,
    "note_speed_mul_affects_travel": true
  }
}
```

### 5. FullBlue Mode（把除 Hold 外全部变 Tap）

配置入口：`mods.full_blue`（也支持别名：`full_blue_mode` / `fullbluemode` / `FullBlueMode`）。

字段：

- `enable`：是否启用（默认 `true`）
- `convert_non_hold_to_tap`：是否把非 Hold 全部改为 Tap（默认 `true`）
- `force_line_alpha`：强制线 alpha（默认 `255`）
- `note_speed_mul_affects_travel`：默认 `true`
- `note_overrides`：FullBlue 内置的 note 全局覆盖（见下文 note_overrides 的字段）

最小示例：

```json
{
  "mods": {
    "full_blue": {
      "enable": true,
      "convert_non_hold_to_tap": true,
      "force_line_alpha": 255
    }
  }
}
```

### 6. Note 规则：mods.rules / mods.note_rules

用于“很多新花样”：按条件筛选一批音符，然后批量修改字段。

规则格式（数组）：

- `filter` / `when`：筛选条件
- `set` / `then`：要修改的字段
- `apply_to_hold`：是否对 Hold 生效（默认 `true`）

filter 支持字段：

- `line_id` / `line_ids`
- `kind` / `kinds`
- `not_kind` / `not_kinds` / `exclude_kind`
- `above`
- `fake`
- `t_hit_min` / `time_min`（秒）
- `t_hit_max` / `time_max`（秒）
- `t_end_min` / `t_end_max`（秒）

set 支持字段：

- `kind`
- `speed_mul`
- `alpha`
- `size`
- `side` / `above`

示例：把 2~10 秒内的 Flick(4) 全改 Tap(1)：

```json
{
  "mods": {
    "rules": [
      {
        "filter": {"kind": 4, "t_hit_min": 2.0, "t_hit_max": 10.0},
        "set": {"kind": 1}
      }
    ]
  }
}
```

### 7. Note 全局覆盖：mods.note_overrides

对所有音符做强制覆盖（优先级高于 `mods.rules`）。

字段：

- `apply_to_hold`（默认 `true`）
- `kind` / `speed_mul` / `alpha` / `size` / `side`

示例：强制所有非 Hold 音符透明度=255、速度=1.2、统一到上侧：

```json
{
  "mods": {
    "note_overrides": {
      "apply_to_hold": false,
      "alpha": 255,
      "speed_mul": 1.2,
      "side": "above"
    }
  }
}
```

### 8. Line 规则：mods.line_rules

用于批量改线的颜色/名称，以及按线强制 alpha。

filter 支持字段：

- `lid` / `lids` / `line_id` / `line_ids`
- `name`

set 支持字段：

- `color`：支持 `[r,g,b]` 或 `"#RRGGBB"`
- `name`
- `force_alpha`：按线强制 alpha（0..1 或 0..255）

示例：只强制 0、3 号线 alpha=255 并染成蓝色：

```json
{
  "mods": {
    "line_rules": [
      {
        "filter": {"line_ids": [0, 3]},
        "set": {"force_alpha": 255, "color": "#00A0FF"}
      }
    ]
  }
}
```

### 9. Hold 转 Tap + 间隔 Drag：mods.hold_to_tap_drag

用于把 Hold 音符“拆解”为：

- Hold 头部的 1 个 Tap（可选）
- Hold 持续期间按固定间隔生成的 Drag（默认 kind=2）

这是一个会**生成新音符**的 mod（不同于 `note_rules` 只改字段）。

配置字段：

- `enable`：是否启用（默认 `true`）
- `interval` / `drag_interval`：生成 Drag 的时间间隔（秒，默认 `0.1`）
- `tap_head`：是否在 hold 起点生成 Tap（默认 `true`）
- `remove_hold`：是否移除原始 Hold（默认 `true`）
- `include_end`：是否在 `t_end` 处补一个 Drag（默认 `true`）
- `drag_kind`：生成的 Drag 类型（默认 `2`，也可改成 `1/4` 等）

示例：把所有 Hold 拆成 Tap + 每 0.12 秒一个 Drag：

```json
{
  "mods": {
    "hold_to_tap_drag": {
      "enable": true,
      "interval": 0.12,
      "tap_head": true,
      "remove_hold": true,
      "include_end": true,
      "drag_kind": 2
    }
  }
}
```

## 分数计算

- **Sequence 模式**：只计算实际播放的音符
- **Composite 模式**：计算所有轨道的唯一音符总数
- 准确度占 90%，连击占 10%
- 全 Perfect = 1,000,000 分

## 注意事项

1. 时间偏移 `time_offset` 用于微调对齐，公式为：
   ```
   主时间 = (谱面时间 + offset - time_offset) / chart_speed + start_at
   ```

2. 谱面速度 `chart_speed` 只影响谱面时间轴，不影响音频播放速度

3. Advance 模式下不使用单谱面的 `--start_time`/`--end_time` 参数

4. 建议使用绝对路径避免路径解析问题

## 故障排除

- **BGM 无法播放**：检查文件路径，尝试将 `mix` 设为 `false`
- **音符不同步**：调整 `time_offset` 参数
- **性能问题**：减少同时播放的轨道数或关闭特效

## 与其他参数组合

Advance Mode 可以与大多数其他参数组合使用：
```bash
python phic_pygame_renderer.py --advance advance.json \
  --respack my_respack.zip \
  --autoplay \
  --chart_speed 1.0 \
  --expand 1.5
```

注意：`--start_time` 和 `--end_time` 在 Advance 模式下无效。
