# 配置参考

> 🌐 [English](CONFIG.md)

本页记录 `include/phigros/config/render_config.hpp` 中当前 `RenderConfig` 的实现语义。

面向使用者的工作流与优先级示例请看 [../../docs/CONFIG_USAGE.zh.md](../../docs/CONFIG_USAGE.zh.md)。

## 输入格式

加载路径为：

- `load_config(path)`
- `load_config_text(text)`
- `load_config_json(json)`

当前行为更准确的描述是：在解析前先去掉 `//` 行注释的 JSON。不要把它当作完整 JSONC 规范实现，应该以代码行为为准。

## 顶层结构

```json
{
  "backend": "...",
  "window": { "w": 1280, "h": 720 },
  "render": { ... },
  "assets": { ... },
  "gameplay": { ... },
  "rpe": { ... },
  "debug": { ... }
}
```

`render.backend` 也会被读取为别名，但如果顶层 `backend` 同时存在，则以顶层为准。

## 从 JSON 加载的字段

### `window`

- `w`：默认 `1280`
- `h`：默认 `720`

### `render`

核心视觉与布局：

- `approach`：默认 `3.0`，截断到 `[0.1, 30.0]`
- `chart_speed`：默认 `1.0`，截断到 `[0.1, 20.0]`
- `expand`：映射到 `expand_factor`，默认 `1.0`
- `note_scale_x`：默认 `2.5`
- `note_scale_y`：默认 `1.0`
- `note_flow_speed_multiplier`：默认 `1.0`
- `note_alpha`：默认 `1.0`，截断到 `[0.0, 1.0]`
- `font_size`：默认 `1.0`，截断到 `[0.5, 3.0]`
- `font_align`：默认 `true`
- `overlay_transparent`：默认 `false`
- `overrender`：默认 `1.0`
- `note_outline`：默认 `false`

可见性与路径行为：

- `no_cull`：默认 `false`
- `no_cull_screen`：默认 `false`
- `no_cull_enter_time`：默认 `true`

判定线 alpha 模式：

- `line_alpha_affects_notes`：`off`、`negative_only` 或 `always`
- 当前 `negative_only` 语义以运行时代码为准，只有在原始判定线 alpha 为负时才会应用判定线 alpha 调制

Hold 与打击特效：

- `hold_body_glow_alpha`：默认 `0.35`，截断到 `[0.0, 1.0]`
- `show_hitfx`：默认 `true`
- `show_particles`：默认 `true`
- `particle_count`：默认 `8`，截断到 `[0, 64]`
- `hitfx_intensity`：默认 `1.0`，截断到 `[0.0, 2.0]`
- `hitfx_effect_apply`：默认 `true`

拖影选项，全部为可选字段：

- `trail_alpha`
- `trail_frames`
- `trail_decay`
- `trail_blur`
- `trail_dim`
- `trail_blur_ramp`
- `trail_blend`
- `trail_blur_quality`
- `trail_chromatic`
- `trail_decay_curve`
- `trail_glow`

运动模糊选项，全部为可选字段：

- `motion_blur_samples`
- `motion_blur_shutter`
- `motion_blur_curve`

后端别名：

- `backend`：若出现在 `render` 下，则写入 `cfg.backend`

### `assets`

- `respack`：映射到 `respack_path`，默认 `./respack.zip`
- `bg`：默认空字符串
- `bg_blur`：默认 `10`
- `bg_dim`：默认 `120`

### `gameplay`

- `autoplay`：默认 `true`
- `hold_tail_tol`：默认 `0.8`
- `hold_fx_interval_ms`：默认 `200`
- `audio_offset_ms`：默认 `0.0`

嵌套 `simulateplay` 对象：

- `enabled`：默认 `false`
- `mode`：默认 `aggressive`
- `max_pointers`：默认 `2`，截断到 `[1, 8]`
- `jitter_ms`：默认 `12.0`，截断到 `[0.0, 80.0]`
- `render_pointer`：默认 `true`
- `render_trail`：默认 `true`
- `trail_seconds`：默认 `0.16`，截断到 `[0.02, 1.0]`
- `cursor_radius_px`：默认 `20.0`，截断到 `[4.0, 80.0]`

### `rpe`

- `rpe_easing_shift`：默认 `0`

### `debug`

- `basic_debug`：默认 `false`

### 顶层 `backend`

- `backend`：默认 `sdl3_bgfx`
- 如果顶层存在该字段，它会覆盖 `render.backend`

## 仅程序设置或尚未完全接线的字段

以下字段存在于 `RenderConfig` 中，但当前不会由 `load_config_json()` 从 JSON 加载：

- `force_line_alpha01`
- `force_line_alpha01_by_lid`
- `note_speed_mul_affects_travel`

在加载器扩展之前，应把它们视为代码层覆盖项。

## 序列化

`config_to_json()` 与 `save_config()` 会把配置重新序列化为 JSON。

当前序列化行为：

- 写出上面展示的规范分区结构
- 总是写出 `gameplay.simulateplay` 嵌套块
- 写出顶层 `backend`
- `assets.bg` 为空时省略
- 拖影 / 运动模糊可选字段在 optional 未设置时省略
- 一些 optional 字段使用简单 truthy 判断写出，因此当前实现下 false 类值也可能被省略

## 相关文档

- [../../docs/CONFIG_USAGE.zh.md](../../docs/CONFIG_USAGE.zh.md)
- [RENDER.zh.md](RENDER.zh.md)
- [DEBUG_FLAGS.zh.md](DEBUG_FLAGS.zh.md)
