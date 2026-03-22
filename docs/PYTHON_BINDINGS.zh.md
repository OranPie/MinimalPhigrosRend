# Python 绑定

> 🌐 [English](PYTHON_BINDINGS.md)

`phigros_cpp` 把 C++ 谱面处理管线暴露给 Python。

它面向谱面加载、预处理、逐帧求值、自动游玩模拟，以及 PHBC 编译 / 读写工作流。不暴露 SDL 窗口层或原生渲染后端对象。

## 构建

在仓库根目录构建 wheel：

```bash
python3 -m pip install -U pip build
python3 -m build
```

直接用 CMake 构建：

```bash
cmake -S cpp -B cpp/build_py -DBUILD_PYTHON_BINDINGS=ON -DBUILD_RENDER_APP=OFF -DUSE_LIBAV=OFF -DUSE_BGFX=OFF
cmake --build cpp/build_py --target _core --parallel
```

从 checkout 直接导入：

```bash
PYTHONPATH=python:cpp/build_py python3
```

## 快速开始

```python
import phigros_cpp as pc

chart = pc.load_chart("charts/MyChart/IN.json", width=1280, height=720)
frame = chart.build_frame(12.5)
result = pc.simulate_autoplay(chart, fps=240.0, mode="aggressive")

print(chart.playable_count, frame.hud.score, result.score.score)
```

## 主要 API 面

顶层辅助函数：

- `load_chart()`
- `scan_charts_directory()`
- `load_config()`
- `config_from_dict()`
- `compute_score()`
- `compile_chart()`
- `read_phbc()` / `write_phbc()`
- `simulate_autoplay()`

主要对象：

- `ChartHandle`
- `RenderConfig`
- `FrameSnapshot`
- `CompiledChart`
- `PhbcWriteOptions`

## 能力边界

包含：

- 谱面解析与发现
- CPU 侧帧快照
- 自动游玩模拟结果
- 配置加载与转换
- PHBC 工作流

不包含：

- SDL 应用 / 窗口 API
- 纹理或 draw-call 访问
- respack 加载接口
- 视频导出绑定

## 相关文档

- 配置工作流：[CONFIG_USAGE.zh.md](CONFIG_USAGE.zh.md)
- 渲染器使用：[CPP_RENDERER.zh.md](CPP_RENDERER.zh.md)
- 内部接口：[../cpp/docs/INTERFACES.zh.md](../cpp/docs/INTERFACES.zh.md)
- 数据结构：[../cpp/docs/DATA_STRUCTURES.zh.md](../cpp/docs/DATA_STRUCTURES.zh.md)
- 格式内部说明：[../cpp/docs/FORMAT.zh.md](../cpp/docs/FORMAT.zh.md)
- 配置内部说明：[../cpp/docs/CONFIG.zh.md](../cpp/docs/CONFIG.zh.md)
