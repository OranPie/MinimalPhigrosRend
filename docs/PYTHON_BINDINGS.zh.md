# Python 绑定

> 🌐 [English](PYTHON_BINDINGS.md)

`phigros_cpp` 包将 C++ 谱面处理流水线暴露给 Python。
它面向谱面加载、预处理、帧求值、自动游玩模拟，以及 PHBC 的编译/读写流程。
它**不**暴露 SDL 窗口、纹理或实际渲染后端。

## 范围

`phigros_cpp` 当前支持：

- 加载 Official、RPE、PEC、文件夹、zip 引用路径和 `.phbc` 谱面
- 通过 `scan_charts_directory()` 扫描谱面
- 通过 `load_config()` 和 `config_from_dict()` 加载配置
- 通过 `ChartHandle.build_frame()` / `ChartHandle.frames()` 做帧求值
- 通过 `simulate_autoplay()` 做自动游玩模拟
- 通过 `compute_score()` 计算分数
- PHBC 编译、读取、写入辅助接口

当前不包含：

- `phigros_render` SDL 应用 / 窗口接口
- respack 加载
- 纹理 / draw call 访问
- 视频导出绑定

## 构建

### Wheel 风格构建

在仓库根目录执行：

```bash
python3.10 -m pip install -U pip build
python3.10 -m build
```

该流程使用 [pyproject.toml](/Users/yanyige/MinimalPhigrosRend/pyproject.toml)，
并通过 `scikit-build-core` 从 `cpp/` 构建扩展模块。

### 开发构建

如果想直接用 CMake 构建扩展：

```bash
cd cpp
cmake -S . -B build_py \
  -DBUILD_PYTHON_BINDINGS=ON \
  -DBUILD_RENDER_APP=OFF \
  -DUSE_LIBAV=OFF \
  -DUSE_BGFX=OFF
cmake --build build_py --target _core -j4
```

然后在仓库根目录下这样导入：

```bash
PYTHONPATH=python:cpp/build_py python3.10
```

## 快速开始

```python
import phigros_cpp as pc

chart = pc.load_chart("charts/MyChart/IN.json", width=1280, height=720)
frame = chart.build_frame(12.5)

print(chart.notes_count, chart.playable_count)
print(frame.hud.score, len(frame.notes))

result = pc.simulate_autoplay(chart, fps=240.0, mode="aggressive")
print(result.score.score, result.max_combo)
```

## 主要 API

### 顶层函数

- `load_chart(path, width=1280, height=720, easing_shift=0, password="") -> ChartHandle`
- `scan_charts_directory(path) -> list[ChartEntry]`
- `load_config(path) -> RenderConfig`
- `config_from_dict(data) -> RenderConfig`
- `compute_score(acc_sum, max_combo, total_notes) -> ScoreResult`
- `compile_chart(chart, sample_rate=240.0) -> CompiledChart`
- `read_phbc(path, password="") -> CompiledChart`
- `write_phbc(compiled, path, options=PhbcWriteOptions()) -> None`
- `simulate_autoplay(chart, fps=240.0, mode="aggressive", max_pointers=2, duration=None) -> AutoplayResult`

### `ChartHandle`

只读元数据：

- `offset`
- `chart_end`
- `playable_count`
- `notes_count`
- `lines_count`
- `config`

方法：

- `build_frame(t, config=None) -> FrameSnapshot`
- `frames(times, config=None) -> list[FrameSnapshot]`
- `compile(sample_rate=240.0) -> CompiledChart`
- `to_dict(include_notes=False, include_lines=False) -> dict`

### `RenderConfig`

绑定当前暴露的、与求值相关的字段包括：

- `window_w`, `window_h`
- `expand_factor`
- `note_scale_x`, `note_scale_y`
- `note_flow_speed_multiplier`
- `note_speed_mul_affects_travel`
- `note_alpha`
- `approach`, `chart_speed`
- `no_cull`, `no_cull_screen`, `no_cull_enter_time`
- `overrender`
- `line_alpha_mode`
- `rpe_easing_shift`

### `FrameSnapshot`

- `t`
- `lines`
- `notes`
- `hud`
- `to_dict()`

每个 line / note snapshot 都会作为类型化对象暴露，同时也支持 `to_dict()`。

## PHBC 示例

```python
import phigros_cpp as pc

chart = pc.load_chart("charts/MyChart/IN.json")
compiled = chart.compile(sample_rate=240.0)

opts = pc.PhbcWriteOptions()
opts.compress = True
opts.compress_algo = pc.CompressionAlgo.ZLIB

pc.write_phbc(compiled, "out.phbc", opts)
compiled2 = pc.read_phbc("out.phbc")
chart2 = compiled2.to_chart()
```

## 说明

- `ChartHandle` 在加载时就会完成预处理。如果你需要不同的宽高，
  或者需要改变会影响预处理结果的配置，应重新加载一个新的 chart handle。
- `build_frame()` 是求值接口，返回的是 CPU 侧快照，而不是像素或绘制命令。
- `simulate_autoplay()` 走的是与原生应用 score-only / autoplay 相同的核心引擎路径。
