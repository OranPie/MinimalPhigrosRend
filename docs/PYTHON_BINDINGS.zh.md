# Python 绑定

> 🌐 [English](PYTHON_BINDINGS.md)

`phigros_cpp` 将 C++ 谱面处理能力暴露给 Python。

它面向谱面加载、预处理、重复帧求值、自动游玩模拟、数据分析，以及 PHBC 编译 / 读写流程；不暴露 SDL 窗口层或原生渲染后端对象。

## 构建

仓库根目录下直接构建 wheel：

```bash
python3 -m pip install -U pip build
python3 -m build
```

安装可选分析依赖：

```bash
python3 -m pip install ".[analysis]"
```

直接使用 CMake 构建：

```bash
cmake -S cpp -B cpp/build_py -DBUILD_PYTHON_BINDINGS=ON -DBUILD_RENDER_APP=OFF -DUSE_LIBAV=OFF -DUSE_BGFX=OFF
cmake --build cpp/build_py --target _core --parallel
```

从源码目录直接导入：

```bash
PYTHONPATH=python:cpp/build_py python3
```

## 快速开始

```python
import phigros_cpp as pc

chart = pc.load_chart("charts/MyChart/IN.json", width=1280, height=720)
frame = chart.frame(12.5)
result = pc.simulate_autoplay(chart, fps=240.0, mode="aggressive")
evaluator = chart.evaluator()
frames = evaluator.build_frames([0.0, 0.5, 1.0])

print(chart.playable_count, frame.hud.score, result.score.score, len(frames))
```

## 主要 API

顶层辅助函数：

- `load_chart()`
- `scan_charts_directory()`
- `load_config()`
- `config_from_dict()`
- `compute_score()`
- `compile_chart()`
- `read_phbc()` / `write_phbc()`
- `simulate_autoplay()`
- `rows_to_numpy()` / `rows_to_pandas()`

主要对象：

- `Chart`
- `FrameEvaluator`
- `AutoplayRun`
- `RenderConfig`
- `FrameSnapshot`
- `CompiledChart`
- `PhbcWriteOptions`

常用分析辅助：

- `chart.notes_data()` / `chart.lines_data()`
- `chart.notes_numpy()` / `chart.notes_pandas()`
- `result.hit_events_data()` / `result.hit_events_numpy()` / `result.hit_events_pandas()`

## 边界

包含：

- 谱面解析与发现
- CPU 侧帧快照
- 自动游玩模拟结果
- 配置加载与转换
- PHBC 工作流
- 面向 NumPy / pandas 的分析导出

不包含：

- SDL 应用 / 窗口 API
- 纹理或 draw-call 访问
- respack 加载接口
- 视频导出绑定
