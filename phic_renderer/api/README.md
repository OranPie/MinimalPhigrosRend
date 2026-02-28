# phic_renderer.api

本目录提供一组 **Python API**，用于在不破坏现有渲染后端（pygame / moderngl）的前提下，做更高层的“编排（orchestrate）”。

目前包含：

- `playlist.py`: 从 `charts/` 目录运行（可能非常大、800+）的谱面播放列表，并支持 Python 条件跳转。

## 目标

- 支持 **超多谱面**（800+）而不一次性把所有谱面完整加载进内存。
- 支持 **Python 自定义条件跳转**（例如打到 N 个 note 立刻跳下一首/跳到指定索引/重洗牌/停止）。
- 支持 **实时播放** 与 **录制**（复用 `phic_renderer.record` 的 `record_*` / `recorder` 参数）。

## Playlist API（核心概念）

Playlist 把“每张谱面”看作一个 `ChartMeta`（轻量元数据），播放时再按需加载真实谱面数据。

### 入口函数（最简单）

- `run_random_playlist(args, charts_dir=..., notes_per_chart=10, seed=None, shuffle=True, switch_mode='hit') -> Judge`

适合快速验证：扫描 `charts_dir`，按设定 shuffle，按 `notes_per_chart` 截取每张谱面的一小段来播放。

### 推荐的可组合 API（脚本/自定义排序过滤更强）

- `build_chart_metas(...) -> List[ChartMeta]`
- `run_playlist(args, metas=..., switch_mode=..., seed=...) -> Judge`

你可以先 `build_chart_metas` 做过滤/排序，然后 `run_playlist` 执行。

## 谱面目录扫描规则（charts_dir）

会扫描 `charts_dir` 下的输入项：

- 含 `info.yml` 的 pack 目录
- `.zip` / `.pez` pack
- “散装谱面文件”：`.json` / `.pec` / `.pe`
- “散装谱面目录”：目录下存在可识别的谱面文件（如 `EZ.json` / `HD.json` / `IN.json` 等）即视作一个输入

对每个输入项，Playlist 只会预加载最小必要信息：

- `chart_info`（尽力读取：曲名/难度/等级等）
- `total_notes`：整张谱面可打的 note 总数
- 段信息（segment）：
  - `seg_notes`：本段包含的 note 数（通常为 `notes_per_chart`）
  - `seg_end_time` / `seg_duration`：本段结束时间
  - `seg_max_chord`：本段最大多押（用于快速过滤）
  - `seg_note_hit_times`：本段 note 的 `t_hit` 列表（用于精确续播定位）

播放时只加载当前谱面到内存。

## 切歌 / 跳转机制

Playlist 提供两套机制，可叠加使用：

### 1) 内置切歌（switch_mode）

- `switch_mode='hit'`：打到 `notes_per_chart` 个 **命中（hit）** 就结束本段
- `switch_mode='judged'`：达到 `notes_per_chart` 个 **判定（judgement = hit + miss）** 就结束本段

### 2) 用户回调跳转（playlist_should_jump）

在 args 上挂一个函数：`args.playlist_should_jump = callable`。

函数签名：

```python
def playlist_should_jump(ctx: dict):
    # return JumpDecision or None
    ...
```

`JumpDecision` 结构：

```python
@dataclass
class JumpDecision:
    action: str  # next | prev | jump | reshuffle | stop
    index: Optional[int] = None
```

回调触发时机：

- **游戏进行中（每帧）**：如果返回了有效 `action`，当前段会立刻停止并执行该决策
- **段结束后**：也会应用返回的决策

`ctx`（尽力提供，字段会随版本增加）：

- `playlist_index`, `playlist_size`
- `meta`：当前 `ChartMeta`
- `t`：当前谱面时间
- `judge`：当前 `Judge`

并且（用于更复杂条件跳转）：

- `last_judge_event`：最新一次判定事件（如果本帧没有新判定则可能为 `None`）
- `judge_events_frame`：本帧内发生的所有判定事件列表

判定事件字段（典型）：

- `grade`：`PERFECT/GOOD/BAD/MISS`
- `t_now`：判定发生的时间
- `t_hit`：note 理论命中时间
- `note_id`：note id
- `note_kind`：note 类型
- `mh`：是否多押（同一时刻多 note）
- `line_id`
- `source`：事件来源（manual/autoplay/hold_finalize 等）
- `hold_percent`：hold 结算进度（0..1 或 None）

## 总分 / 进度条正确性（跨谱面总览）

为了让 UI 的 `SCORE/MAX` 与进度条在“整条 playlist”维度上保持正确：

- Playlist 会在调用 renderer 时设置：
  - `total_notes_override = sum(seg_notes)`
  - `chart_end_override = sum(seg_duration)`
  - `ui_time_offset`：累计段时长作为 UI 时间偏移

这样你看到的是整条列表的总分与总进度，而不是单曲。

## 精确续播（从累计 combo/hit 定位到谱面内时间点）

Playlist 支持“跨谱面累计 hit/combo 的精确定位”，核心做法是：

- `ChartMeta.seg_note_hit_times` 记录了段内每个 note 的命中时间
- 通过 `--playlist_start_from_combo_total`（或脚本内部参数）把“累计命中数”映射到：
  - 从第几首开始
  - 本段从哪个 `t_hit` 对应的时间点开始播放
  - 需要跳过的 note 数（用于统计对齐）

## 录制支持（Recording）

Playlist 复用 `phic_renderer.record` 的录制机制：

- 只要传入 `args.record_enabled=True` 并提供 `recorder`（或 `record_dir`），即可 headless 渲染并写帧。

推荐用 `FrameRecorder` 或 `VideoRecorder`：

```python
from phic_renderer.recording.frame_recorder import FrameRecorder

rec = FrameRecorder('out_frames', W, H, fps)
rec.open()
setattr(args, 'recorder', rec)
setattr(args, 'record_enabled', True)
setattr(args, 'record_headless', True)
```

## Playlist Script（通过 CLI 加载 Python 脚本）

你可以在以下入口通过 `--playlist_script` 直接运行一个 Python 脚本来控制 playlist：

- `phic_renderer`（主程序）
- `phic_renderer.record`（录制入口）

脚本可选导出（“标准 playlist script 接口”）：

- `configure_args(parser)`：添加脚本自定义参数
- `build_metas(args, charts_dir, W, H)`：自定义构建 metas（可选）
- `sort_metas(metas, args)`：自定义排序
- `playlist_filter(meta, args) -> bool`：自定义过滤
- `playlist_should_jump(ctx) -> Optional[JumpDecision]`：自定义跳转

项目根目录的 `play_charts_sorted.py` 已经是一个标准 script 示例。

## 备注 / 限制

- 当前实现为了算出 `ChartMeta`，会对每张谱面至少调用一次 `load_chart()`：
  - 对 800+ 谱面仍是 **O(N)** 的磁盘 IO（内存安全但启动可能较慢）
  - 需要更快启动的话，可以再加一个 `ChartMeta` 的 JSON 缓存文件
- pygame backend 支持 `reuse_pygame=True`：段与段之间复用窗口
- 对 pygame backend，Playlist 会创建并复用一个共享音频后端（`reuse_audio=True`），并在播放结束后关闭
