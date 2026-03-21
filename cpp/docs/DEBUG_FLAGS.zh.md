# Debug Flags 参考

> 🌐 [English](DEBUG_FLAGS.md)

本文档说明 C++ 渲染器支持的全部 `--debug-flags` 标记及其行为。

## 用法

使用 `--debug-flags`（或 `--debug_flags`）传入一个或多个标记。

```bash
./phigros_render chart.json --debug-flags FRAME_TIME|AUDIO_INFO|JUDGE_LINE_NUMBER
```

规则：

- 分隔符支持：`|`、`,`、`+`
- 标记规范化：`-`、空格、`.` 会自动转换为 `_`
- `ALL` 表示启用全部调试标记
- 也支持直接传入数值位掩码

## 标记列表

### 时间 / 运行态面板

| 标记 | 行为 |
|------|------|
| `FRAME_TIME` | 在左上角绘制面板，显示帧耗时、FPS、谱面时间和渲染分辨率。 |
| `TIMING_WINDOWS` | 绘制 Perfect / Good / Bad 判定窗（毫秒）的说明面板。 |
| `TIMING_CLOCKS` | 绘制多行时钟面板，显示模拟时间、音频游标、时间漂移、render dt、sim dt、每渲染帧模拟步数、当前模式、暂停状态和结算状态。 |
| `PERFORMANCE_PROFILER` | 在右侧绘制性能分析面板，显示各个帧阶段的平均耗时与最大耗时。 |
| `FRAME_TIME_GRAPH` | 绘制最近帧耗时的滚动折线图。 |
| `VISIBILITY_SUMMARY` | 绘制可见性汇总面板，显示各类可见音符数、多押数、pending/judged/miss 数，以及激活中的 Hold 数。 |
| `RECORDING_STATUS` | 绘制录制状态面板，显示目标 FPS、编码器墙钟 FPS、队列占用、采集/输出分辨率和写帧耗时；未录制时显示配置目标值。 |

### 音频面板

| 标记 | 行为 |
|------|------|
| `AUDIO_INFO` | 绘制音频状态面板，显示 BGM 是否加载/播放、hitsound 可用数、音频游标、是否已启动播放以及活跃输入点数量。 |
| `AUDIO_WAVEFORM` | 基于 PCM tap 在当前播放时间附近绘制波形面板；若不可用则显示 `PCM tap unavailable`。 |
| `AUDIO_SPECTRUM` | 基于最近 PCM 采样绘制简易频谱柱状图；若不可用则显示 `PCM tap unavailable`。 |

### 判定线覆盖层

| 标记 | 行为 |
|------|------|
| `JUDGE_LINE_INFO_WINDOW` | 在左侧绘制面板，列出所有可见判定线的位置、旋转、alpha、scroll 和缩放。 |
| `JUDGE_LINE_INFO_ABOVE_LINE` | 在每条判定线上方绘制简短文字，显示线 id、alpha、scroll、旋转和 X/Y 缩放。 |
| `JUDGE_LINE_NUMBER` | 在可见判定线附近绘制判定线 id。 |
| `LINE_GEOMETRY` | 绘制判定线几何线段以及两端端点标记。 |
| `LINE_INFO_COLOR_MAPPING` | 让部分判定线相关调试文字使用判定线运行时颜色；未启用时使用中性色。 |
| `LINE_ALPHA_BAR` | 在每条判定线下方绘制小型 alpha 条，填充长度与运行时 alpha 成比例。 |
| `SCROLL_SPEED_OVERLAY` | 在每条判定线旁绘制当前 scroll 数值文本。 |
| `SPEED_VISUALIZATION` | 根据逐帧位移绘制判定线运动向量，并标注 scroll 变化量。 |
| `LINE_ACTIVITY_PANEL` | 在右侧绘制“line activity”面板，显示最活跃的几条判定线，包括可见音符数、即将命中的音符数、激活 Hold 数、alpha 和 scroll。 |

### 音符覆盖层

| 标记 | 行为 |
|------|------|
| `NOTE_LINE_NUMBER` | 在每个可见音符旁绘制音符 id。 |
| `NOTE_INFO` | 在音符附近绘制详细信息：音符 id、所属判定线 id、种类、命中时间、世界坐标、alpha、尺寸，以及 Hold 的进度。 |
| `NOTE_JUDGE_WINDOW` | 在每个音符旁绘制 `dt` 文本，显示距离命中的毫秒数和音符类型，并按判定窗接近程度着色。 |
| `NOTE_HITBOX` | 为每个可见音符绘制包围框；对 Hold 还会额外绘制头尾连线。 |
| `NOTE_TRAIL` | 使用最近几帧记忆的位置，为每个可见音符绘制短拖尾轨迹。 |
| `NOTE_APPROACH_GUIDE` | 为未判定音符绘制一条到其所属判定线的引导线。 |
| `NOTE_DENSITY_GRAPH` | 绘制一个小型柱状图，展示前方固定时间窗口内的音符密度。 |
| `VELOCITY_VECTORS` | 根据逐帧位移绘制音符运动向量。 |
| `COMBO_ZONES` | 为处于 Bad/Good/Perfect 接近窗口中的音符绘制局部框，颜色随当前窗口变化。 |
| `SIMULTANEOUS_INDICATOR` | 为被标记为 simultaneous / multi-hit（`mh`）的音符绘制菱形标记。 |
| `MH_TEXTURE_STATUS` | 对可见的 `mh` 音符绘制文字，显示其种类，以及当前是否正在使用 MH 纹理路径（`mh-tex`）还是回退到基础纹理（`base-tex`）。 |

### 输入 / 交互覆盖层

| 标记 | 行为 |
|------|------|
| `TOUCH_VISUALIZATION` | 将活跃输入点绘制为方框、速度向量，并显示 slot id 和峰值速度。 |
| `CENTER_CROSSHAIR` | 在屏幕中心绘制十字准星与中心点。 |
| `EXPAND_BORDER` | 当 `expand_factor > 1` 时，标注原始视口边界并显示 expand 倍率。 |

### 分数 / 判定覆盖层

| 标记 | 行为 |
|------|------|
| `SCORE_BREAKDOWN` | 绘制分数面板，显示 Perfect/Good/Bad/Miss 数、连击/最大连击、已判定数量、准确率和分数。 |
| `JUDGMENT_HISTORY` | 绘制一个会渐隐的最近判定列表，并按结果着色。 |
| `MISS_INDICATOR` | 在最近 Miss 的音符位置绘制短暂的红色闪框和 `MISS` 标签。 |

### Hold 专用覆盖层

| 标记 | 行为 |
|------|------|
| `HOLD_STATE` | 为可见 Hold 绘制进度条，并区分当前是否正在按住、是否已完成、是否提前松手。 |

### 谱面 / 模式状态面板

| 标记 | 行为 |
|------|------|
| `CHART_METADATA` | 绘制谱面信息面板，显示判定线数量、可玩/假音符数量、各类音符总数、offset 和时长。 |
| `MIRROR_STATUS` | 绘制一个小面板，显示当前镜像模式是否开启。 |

## 说明

- 多个标记可以自由组合。
- 某些覆盖层依赖运行时状态，只有在条件满足时才会显示，例如：
  - `MH_TEXTURE_STATUS` 只会给已经被标记为 `mh` 的音符打标签
  - `HOLD_STATE` 只会对当前可见的 Hold 生效
  - `FRAME_TIME_GRAPH` 和 `NOTE_TRAIL` 需要积累几帧历史数据后才更有意义
- `LINE_INFO_COLOR_MAPPING` 属于“修饰型”标记：它主要改变部分判定线调试文字的颜色，而不是单独绘制一个独立覆盖层。
