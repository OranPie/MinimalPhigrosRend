# 数据结构

> 🌐 [English](DATA_STRUCTURES.md)

本页覆盖解析、引擎、渲染与绑定共享的核心数据模型。

## 核心谱面模型

### `Note`

关键字段：

- 标识：`nid`、`line_id`、`kind`
- 放置：`above`、`x_local_px`、`y_offset_px`
- 时间：`t_hit`、`t_end`、`visible_time`
- 外观：`size_px`、`alpha01`、`tint_rgb`、可选 `tint_hitfx_rgb`
- 运行时缓存：`scroll_hit`、`scroll_end`、`t_enter`、`mh`
- 格式扩展：`hitsound_path`、`speed_mul`、`fake`

### `Line`

一条判定线持有时变求值器以及 RPE 专用展示状态：

- 基础轨道：`pos_x`、`pos_y`、`rot`、`alpha`、`scroll_px`
- 编译态覆盖：`scroll_fn`、`compiled_color`
- 展示状态：`color_rgb`、`texture_path`、`text`、`anchor`、`name`
- 层级 / 状态：`father`、`rotate_with_father`、`attach_ui`、`z_order`、`is_cover`
- RPE 控制事件：`alpha_ctrl`、`pos_ctrl`、`size_ctrl`、`y_ctrl`、`skew_ctrl`

### `ChartData`

规范解析后谱面容器：

- `offset`
- `lines`
- `notes`
- 可选的 RPE 元数据资源路径
- 缓存的 `chart_end_t` 与 `playable_count`
- 表示 PHBC 来源的 `is_compiled` 标志
- 索引：`early_notes`、`notes_by_enter`

## 运行时状态

### `NoteState`

每个音符的可变判定状态：

- 当前判定状态：`judged`、`hit`、`miss`、`judge_grade`
- Hold 生命周期：`holding`、`released_early`、`hold_finalized`、`hold_failed`、`hold_grade`
- 时间缓存：`judge_t`、`judge_delta_ms`、`release_t`、`next_hold_fx_ms`

### `PreparedChart`

绑定 / API 侧打包后的输入：

- `ChartData chart`
- `RenderConfig config`
- `scoring_notes`
- `simulation_end`

## 谱面发现与格式容器

### `ChartAssets` 与 `ChartEntry`

用于谱面发现与资源解析：

- `ChartAssets`：音乐路径、插图路径、额外文件
- `ChartEntry`：显示名、难度、谱面路径、资源、来源类型

### `CompiledChartData`

编译 / 预采样后的谱面表示：

- 谱面级元数据：`offset`、`chart_end_t`、`playable_count`、`sample_rate`、`t_start`、`sample_count`
- `CompiledLine` 中的 `pos_x`、`pos_y`、`rot`、`alpha`、`scroll` 与可选动态颜色数组
- `notes` 作为普通 note 记录保存，`t_enter` 已烘焙

`CompiledChartData::to_chart_data()` 会通过 `SampledTrack` lambda 把它重新包装为普通 `ChartData`。

## 帧快照

### `LineSnapshot`

逐帧判定线求值：

- 变换：`x`、`y`、`rot`、`cos_rot`、`sin_rot`
- 可见 / 颜色：`alpha01`、`scroll`、`color`
- 展示：`incline`、`scale_x`、`scale_y`、`texture_path`、`text`
- 排序：`is_cover`、`z_order`

### `NoteSnapshot`

逐帧可见音符状态：

- 头 / 尾世界坐标
- 音符 alpha、判定线旋转、尺寸、颜色
- Hold 标志、判定 / miss 标志、多押标志、skew

### `FrameSnapshot`

顶层 CPU 帧载荷：

- `t`
- `lines`
- `notes`
- `hud`

## 不变量

以下假设在整个代码库中都很重要：

- `ChartData.notes` 在解析后按 `t_hit` 排序。
- `playable_count` 只统计 `fake == false` 的音符。
- `chart_end_t` 由音符结束时间导出。
- `mh` 在 `ChartData::finalize()` 中为同一时刻的可判定音符赋值。
- `early_notes` 与 `notes_by_enter` 按 `t_enter` 排序，用于可见性 / 候选选择。
- 编译谱面会设置 `is_compiled = true`，使调用方跳过 `precompute_t_enter()`。
