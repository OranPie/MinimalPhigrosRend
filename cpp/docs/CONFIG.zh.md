# 配置参考

> 🌐 [English](CONFIG.md)

所有选项从 JSONC 文件（支持 `//` 和 `#` 行注释的 JSON）中加载。

```bash
./phigros_render chart.json --config config.jsonc
```

---

## 顶层结构

```jsonc
{
  "window":   { ... },
  "render":   { ... },
  "assets":   { ... },
  "gameplay": { ... },
  "rpe":      { ... },
  "debug":    { ... }
}
```

---

## `window`

| 键 | 类型 | 默认值 | 描述 |
|----|------|--------|------|
| `w` | int | `1280` | 窗口 / 输出宽度（像素） |
| `h` | int | `720`  | 窗口 / 输出高度（像素） |

---

## `render`

### 核心视觉

| 键 | 类型 | 默认值 | 描述 |
|----|------|--------|------|
| `approach` | float | `3.0` | 音符进场时间窗口（秒），范围限制 `[0.1, 30]`。 |
| `chart_speed` | float | `1.0` | 回放速度倍率，范围限制 `[0.1, 20]`。 |
| `expand` | float | `1.0` | 摄像机缩放 / 游戏区域扩展系数。 |
| `overrender` | float | `1.0` | 屏外裁剪边距的超渲染倍率。 |
| `no_cull` | bool | `false` | 禁用所有音符裁剪（每帧渲染全部音符）。 |
| `no_cull_screen` | bool | `false` | 仅禁用屏幕边界裁剪（保留进入时刻裁剪）。 |
| `no_cull_enter_time` | bool | `true` | 设为 `false` 时，跳过尚未到达 `t_enter` 时刻的音符（优化高密度谱面）。默认 `true` 保留所有已进入判定窗口的音符，无论其屏幕位置如何。 |
| `note_scale_x` | float | `2.5` | 音符水平尺寸倍率。 |
| `note_scale_y` | float | `1.0` | 音符垂直尺寸倍率。 |
| `note_flow_speed_multiplier` | float | `1.0` | 单个音符滚动速度倍率。 |
| `note_speed_mul_affects_travel` | bool | `false` | RPE：单音符 `speed_mul` 影响进场距离。 |
| `note_alpha` | float | `1.0` | 全局音符不透明度倍率，范围限制 `[0, 1]`。 |
| `note_outline` | bool | `false` | 在音符前以 1.08× 音符大小绘制深色轮廓。 |

### 判定线 alpha → 音符 alpha

| 键 | 类型 | 默认值 | 描述 |
|----|------|--------|------|
| `line_alpha_affects_notes` | string | `"negative_only"` | 判定线 alpha 调制可见音符 alpha 的方式。 |

`line_alpha_affects_notes` 的可选值：

| 值 | 行为 |
|----|------|
| `"off"` | 音符不受判定线 alpha 影响 |
| `"negative_only"` | 判定线 alpha < 0.5 时音符变暗（默认 Phigros 行为） |
| `"always"` | 始终使音符 alpha = `note.alpha * line.alpha` |

### 打击特效

| 键 | 类型 | 默认值 | 描述 |
|----|------|--------|------|
| `show_hitfx` | bool | `true` | 在音符判定时渲染打击特效精灵 / 光圈。 |
| `show_particles` | bool | `true` | 在音符判定时渲染粒子爆发效果。 |
| `particle_count` | int | `8` | 每次打击的粒子数量，范围限制 `[0, 64]`。 |
| `hitfx_intensity` | float | `1.0` | 所有打击特效的 alpha 倍率，范围限制 `[0, 2]`。 |
| `hitfx_effect_apply` | bool | `true` | 打击特效是否参与拖影/运动模糊通道。`false` 时会在合成后单独绘制（更清晰、不拖尾）。 |

### 拖影效果

| 键 | 类型 | 默认值 | 描述 |
|----|------|--------|------|
| `trail_alpha` | float | *（禁用）* | 拖影槽的基础 alpha，设置后启用拖影。 |
| `trail_frames` | int | `6` | 拖影缓冲槽数量。 |
| `trail_decay` | float | `0.75` | 每槽 alpha 衰减系数（第 N 槽 = `trail_alpha × decay^N`）。 |
| `trail_blur` | int | `0` | 每槽模糊的下采样系数（0 = 不模糊）。 |
| `trail_dim` | int | `0` | 每个历史槽的变暗叠加强度（0–255）。 |
| `trail_blur_ramp` | bool | `false` | 对越旧的槽增加模糊量。 |
| `trail_blend` | string | `"blend"` | 合成模式：`"blend"` 或 `"add"`。 |

### 运动模糊

| 键 | 类型 | 默认值 | 描述 |
|----|------|--------|------|
| `motion_blur_samples` | int | *（禁用）* | 子帧采样数，设置后启用运动模糊。 |
| `motion_blur_shutter` | float | `0.5` | 快门角度比例 `[0, 1]`。 |

---

## `assets`

| 键 | 类型 | 默认值 | 描述 |
|----|------|--------|------|
| `respack` | string | `"./respack.zip"` | 资源包 ZIP 文件路径。 |
| `bg` | string | *（无）* | 背景图片路径（PNG/JPG）。 |
| `bg_blur` | int | `10` | 背景模糊下采样系数（0 = 不模糊）。 |
| `bg_dim` | int | `120` | 背景变暗叠加层不透明度（0–255）。 |

---

## `gameplay`

| 键 | 类型 | 默认值 | 描述 |
|----|------|--------|------|
| `autoplay` | bool | `true` | 自动以全 Perfect 判定所有音符。 |
| `hold_tail_tol` | float | `0.8` | 长按尾部释放容差（占长按时长的比例）。 |
| `hold_fx_interval_ms` | int | `200` | 长按 tick 打击特效的最小间隔（毫秒）。 |
| `audio_offset_ms` | float | `0.0` | 音频延迟补偿，正值表示音符提前触发。 |

---

## `rpe`

| 键 | 类型 | 默认值 | 描述 |
|----|------|--------|------|
| `rpe_easing_shift` | int | `0` | RPE 缓动索引偏移，用于兼容非标准导出。 |

---

## `debug`

| 键 | 类型 | 默认值 | 描述 |
|----|------|--------|------|
| `basic_debug` | bool | `false` | 在屏幕上叠加显示 FPS 和可见音符数量。 |

---

## 强制判定线 alpha 覆盖

以下选项通过 C++ API 或程序方式设置：

- `force_line_alpha01` — 将所有判定线强制设为同一 alpha 值 `[0, 1]`
- `force_line_alpha01_by_lid` — `{line_id: alpha}` 映射，用于逐线覆盖

---

## 完整示例

```jsonc
// MinimalPhigrosRend C++ 配置
{
  "window": {
    "w": 1920,
    "h": 1080
  },

  "render": {
    "approach": 3.0,
    "chart_speed": 1.0,
    "expand": 1.0,
    "note_scale_x": 2.5,
    "note_scale_y": 1.0,
    "note_alpha": 1.0,
    "note_outline": false,
    "line_alpha_affects_notes": "negative_only",

    "show_hitfx": true,
    "show_particles": true,
    "particle_count": 8,
    "hitfx_intensity": 1.0,
    "hitfx_effect_apply": true,

    // 拖影：取消注释以启用
    // "trail_alpha": 0.4,
    // "trail_frames": 6,
    // "trail_decay": 0.7,

    // 运动模糊：取消注释以启用
    // "motion_blur_samples": 4,
    // "motion_blur_shutter": 0.5
  },

  "assets": {
    "respack": "./respack.zip",
    "bg_blur": 10,
    "bg_dim": 120
  },

  "gameplay": {
    "autoplay": true,
    "hold_tail_tol": 0.8,
    "hold_fx_interval_ms": 200,
    "audio_offset_ms": 0.0
  },

  "debug": {
    "basic_debug": false
  }
}
```

---

## 往返序列化

C++ API 可将当前配置保存回 JSON：

```cpp
#include "phigros/config/render_config.hpp"
phigros::config::save_config("out.json", cfg);
```
