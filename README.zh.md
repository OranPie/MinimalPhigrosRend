# MinimalPhigrosRend

> 🌐 [English](README.md)

MinimalPhigrosRend 是一个以现代 C++ 核心为中心的 Phigros 谱面渲染与谱面处理工具集。

当前仓库主要包含：

- `phigros_render`：原生 C++ 渲染器 / 应用，面向桌面与 WebAssembly
- `phigros_cpp`：Python 绑定，提供谱面加载、预处理、自动游玩模拟与 PHBC 工作流
- 一组本地辅助工具，用于预览、Web UI 导出、谱面扫描、mod / chartscript 工作流

如果你需要一个总入口来回答“怎么构建？”和“怎么使用？”，先看这份文档。更细的参考文档会在下面分流。

## 仓库结构

- [`cpp/`](/Users/yanyige/MinimalPhigrosRend/cpp)：C++ 源码、CMake 工程、测试、平台封装
- [`docs/`](/Users/yanyige/MinimalPhigrosRend/docs)：渲染器、chartscript、Python 绑定等说明
- [`config/`](/Users/yanyige/MinimalPhigrosRend/config)：示例配置文件
- [`scripts/`](/Users/yanyige/MinimalPhigrosRend/scripts)：WASM 服务、本地 Web UI 等辅助脚本
- [`python/`](/Users/yanyige/MinimalPhigrosRend/python)：`phigros_cpp` 的 Python 包源码
- [`charts/`](/Users/yanyige/MinimalPhigrosRend/charts)：本地谱面库，供测试和手动运行使用

## 我应该用哪个入口

- 需要渲染、交互游玩、截图、录屏、回放、WASM 构建时，用 `phigros_render`。
- 需要在 Python 里做谱面解析、逐帧求值、自动游玩模拟或 PHBC 读写时，用 `phigros_cpp`。
- 需要播放列表或多谱面批处理时，用 ChartScript。

## 依赖要求

本地最低要求：

- CMake 3.16+
- 支持 C++17 的编译器
- Python 3.10+（用于绑定与辅助脚本）

说明：

- 桌面构建在本地缺少依赖时，会通过 CMake `FetchContent` 拉取部分依赖。
- `USE_SDL3=ON` 时，原生应用会拉取 SDL3。
- `USE_SDL3=OFF` 时，桌面回退到 SDL2，Web 构建使用 Emscripten 自带 SDL2。
- 视频导出优先使用系统 libav；如果不可用，则退回到 `ffmpeg` 子进程方案。

## 快速开始

### 1. 构建原生渲染器

```bash
cmake -S cpp -B cpp/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DUSE_BGFX=OFF
cmake --build cpp/build --parallel
```

产物：

- `cpp/build/phigros_render`

### 2. 运行谱面

```bash
./cpp/build/phigros_render charts/MyChart/IN.json
```

常用变体：

```bash
./cpp/build/phigros_render charts/MyChart/IN.json --score-only
./cpp/build/phigros_render charts/MyChart/IN.json --play
./cpp/build/phigros_render charts/MyChart/IN.json --record out.mp4
./cpp/build/phigros_render charts/MyChart/IN.json --config config/config.jsonc
```

### 3. 运行测试

```bash
./cpp/build/test_easing
./cpp/build/test_engine
./cpp/build/test_parser charts
```

如果你的克隆不包含 [`charts/`](/Users/yanyige/MinimalPhigrosRend/charts)，解析器的自动发现覆盖范围会受限。

## 开发工作流

### 原生渲染器开发

适合本地调试的构建：

```bash
cmake -S cpp -B cpp/build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DUSE_BGFX=OFF
cmake --build cpp/build --parallel
```

常用 CMake 开关：

- `-DBUILD_RENDER_APP=ON|OFF`：是否构建原生渲染器可执行文件
- `-DBUILD_PYTHON_BINDINGS=ON|OFF`：是否构建 Python 扩展目标
- `-DUSE_SDL3=ON|OFF`：原生桌面用 SDL3，否则回退路径
- `-DUSE_BGFX=ON|OFF`：启用 bgfx 后端相关工作
- `-DUSE_LIBAV=ON|OFF`：可用时使用 FFmpeg 开发库
- `-DUSE_LZMA=ON|OFF`：启用 PHBC 的 LZMA 压缩支持
- `-DUSE_ENCRYPTION=ON|OFF`：启用基于 OpenSSL 的 PHBC 加密
- `-DUSE_SANITIZERS=ON|OFF`：开发期启用 ASan / UBSan

### Python 绑定开发

在仓库根目录构建 wheel：

```bash
python3 -m pip install -U pip build
python3 -m build
```

直接用 CMake 构建：

```bash
cmake -S cpp -B cpp/build_py \
  -DBUILD_PYTHON_BINDINGS=ON \
  -DBUILD_RENDER_APP=OFF \
  -DUSE_BGFX=OFF \
  -DUSE_LIBAV=OFF
cmake --build cpp/build_py --target _core --parallel
```

从仓库目录直接导入：

```bash
PYTHONPATH=python:cpp/build_py python3
```

最小冒烟测试：

```python
import phigros_cpp as pc
chart = pc.load_chart("charts/MyChart/IN.json")
print(chart.playable_count, chart.chart_end)
```

### WASM 构建

```bash
cd cpp
./scripts/build_web.sh Release
cd ..
python3 scripts/serve.py --dir cpp/build_web
```

这需要先激活 Emscripten 环境。辅助脚本会替你调用 `emcmake` 和 `emmake`。

## 使用工作流

### 渲染器 CLI

常见任务：

- 自动游玩 / 无头计分：`--score-only`
- 交互游玩：`--play`
- 录制视频：`--record out.mp4`
- 保存 / 播放回放：`--save-replay` 和 `--play-replay`
- 基准测试：`--benchmark --benchmark-iterations N`
- 播放列表脚本：`--script file.chartscript.json`
- 编译为 PHBC：`--compile out.phbc`

详细 CLI 与配置说明：

- [`docs/CPP_RENDERER.md`](/Users/yanyige/MinimalPhigrosRend/docs/CPP_RENDERER.md)
- [`cpp/README.md`](/Users/yanyige/MinimalPhigrosRend/cpp/README.md)

### 配置文件

共享示例配置位于：

- [`config/config.jsonc`](/Users/yanyige/MinimalPhigrosRend/config/config.jsonc)

渲染器配置参考：

- [`cpp/docs/CONFIG.md`](/Users/yanyige/MinimalPhigrosRend/cpp/docs/CONFIG.md)
- [`docs/CPP_RENDERER.md`](/Users/yanyige/MinimalPhigrosRend/docs/CPP_RENDERER.md)

### Python API 使用

主参考文档：

- [`docs/PYTHON_BINDINGS.md`](/Users/yanyige/MinimalPhigrosRend/docs/PYTHON_BINDINGS.md)

### 本地辅助工具

- TUI 启动器：`python3 cpp/scripts/launcher.py`
- WebAssembly 预览服务：`python3 scripts/serve.py`
- 基于浏览器的本地预览 / 导出 Web UI：`python3 scripts/webui.py`

辅助脚本依赖：

- `cpp/scripts/launcher.py` 需要 `textual`
- `scripts/webui.py` 需要 `flask`

脚本入口：

- [`cpp/scripts/launcher.py`](/Users/yanyige/MinimalPhigrosRend/cpp/scripts/launcher.py)
- [`scripts/serve.py`](/Users/yanyige/MinimalPhigrosRend/scripts/serve.py)
- [`scripts/webui.py`](/Users/yanyige/MinimalPhigrosRend/scripts/webui.py)

## 文档索引

- 原生渲染器快速开始：[`cpp/README.md`](/Users/yanyige/MinimalPhigrosRend/cpp/README.md)
- 原生渲染器完整参考：[`docs/CPP_RENDERER.md`](/Users/yanyige/MinimalPhigrosRend/docs/CPP_RENDERER.md)
- ChartScript 参考：[`docs/CHARTSCRIPT.md`](/Users/yanyige/MinimalPhigrosRend/docs/CHARTSCRIPT.md)
- Python 绑定：[`docs/PYTHON_BINDINGS.md`](/Users/yanyige/MinimalPhigrosRend/docs/PYTHON_BINDINGS.md)
- 核心架构：[`cpp/docs/ARCHITECTURE.md`](/Users/yanyige/MinimalPhigrosRend/cpp/docs/ARCHITECTURE.md)
- 配置参考：[`cpp/docs/CONFIG.md`](/Users/yanyige/MinimalPhigrosRend/cpp/docs/CONFIG.md)
- 调试标志：[`cpp/docs/DEBUG_FLAGS.md`](/Users/yanyige/MinimalPhigrosRend/cpp/docs/DEBUG_FLAGS.md)
- 谱面加载器内部说明：[`cpp/docs/CHART_LOADER.md`](/Users/yanyige/MinimalPhigrosRend/cpp/docs/CHART_LOADER.md)

## 当前状态说明

- 当前仓库里，实际可用且有持续文档维护的运行路径是 C++ 渲染器和 `phigros_cpp` 绑定。
- 部分旧文档仍会提到历史上的 Python `phic_renderer` 工作流；若当前 checkout 中没有那部分代码，应视为遗留说明。
