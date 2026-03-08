# MinimalPhigrosRend — C++ 渲染器

> 🌐 [English](README.md)

用 C++17 重写的高性能 Phigros 谱面渲染器。  
跨平台支持：桌面端（SDL3）、Web（WASM/Emscripten）、移动端。

## 功能特性

- 解析 **Official**、**RPE** 和 **PEC** 谱面格式
- 自动游玩，支持打击特效渲染（hitfx + 粒子效果）
- 交互式游玩模式（`--play`），支持键盘/触屏输入
- 回放录制与回放（`--save-replay` / `--play-replay`）
- 通过 FFmpeg 子进程导出视频（`--record`）
- 拖尾特效、动态模糊、音符描边、长按发光
- bgfx GPU 渲染后端（`--backend bgfx`）
- 支持构建为 WASM 在浏览器中部署

## 快速开始

### 构建（桌面端 SDL3）

```bash
cd cpp
mkdir build && cd build
cmake .. -DUSE_BGFX=OFF -DUSE_SDL3=ON
make -j$(nproc)
```

### 运行

```bash
# 自动游玩（仅无窗口计分）
./phigros_render charts/MyChart/IN.json --score-only

# 自动游玩（带窗口）
./phigros_render charts/MyChart/IN.json

# 交互式游玩模式
./phigros_render charts/MyChart/IN.json --play

# 性能基准测试（20 次迭代，无窗口）
./phigros_render charts/MyChart/IN.json --benchmark --benchmark-iterations 20

# 录制视频（需要 PATH 中存在 FFmpeg）
./phigros_render charts/MyChart/IN.json --record output.mp4 --record-preset medium
```

### 按键绑定（游玩模式）

| 按键 | 功能 |
|------|------|
| D / F / J / K | 打击音符 |
| Space | 暂停 / 继续 |
| R | 重新开始 |
| Esc | 退出 |

## 构建选项

| CMake 标志 | 默认值 | 说明 |
|-----------|--------|------|
| `USE_SDL3` | `ON` | 使用 SDL3（OFF = 为 WASM 使用 SDL2） |
| `USE_BGFX` | `OFF` | 启用 bgfx GPU 后端 |

### WASM 构建

```bash
emcmake cmake .. -DUSE_SDL3=OFF -DUSE_BGFX=OFF
emmake make -j$(nproc)
# 输出：phigros.html + phigros.js + phigros.wasm
```

## 配置

渲染器行为通过 JSONC 配置文件控制：

```bash
./phigros_render charts/MyChart/IN.json --config my_config.jsonc
```

所有配置字段的完整参考请参阅 [`docs/CONFIG.md`](docs/CONFIG.md)。

**示例配置**（保存为 `config.jsonc`）：

```jsonc
{
  "window": { "w": 1280, "h": 720 },
  "render": {
    "approach": 3.0,
    "note_scale_x": 2.5,
    "note_outline": true,
    "line_alpha_affects_notes": "negative_only"
  },
  "assets": {
    "respack": "./respack.zip",
    "bg_dim": 120
  },
  "gameplay": {
    "autoplay": true
  }
}
```

## 命令行参考

```
./phigros_render <chart.json> [options]

输入：
  --config <path>              JSONC 配置文件

回放控制：
  --play                       交互式游玩模式
  --score-only                 输出分数后退出（无窗口）
  --headless                   无窗口运行（用于基准测试/CI）

回放录制：
  --save-replay <path>         游玩结束后将回放保存到文件
  --play-replay <path>         加载并回放已保存的回放

视频录制：
  --record <output.mp4>        录制到视频文件（需要 FFmpeg）
  --record-preset <name>       FFmpeg 预设（ultrafast/medium/slow）
  --record-fps <int>           录制帧率（默认：60）
  --record-start <sec>         录制起始时间
  --record-end <sec>           录制结束时间

基准测试：
  --benchmark                  以基准测试模式运行（无窗口）
  --benchmark-iterations <N>   基准测试轮数（默认：3）

截图：
  --screenshot-dir <path>      保存 PNG 帧的目录

渲染后端：
  --backend sdl3|bgfx          渲染后端（默认：sdl3）

音频：
  --audio <path>               背景音乐文件路径
  --audio-offset <ms>          音频延迟补偿（毫秒）
```

## 测试

```bash
# 引擎测试（6343 项检查）
cd cpp/build
./test_engine --auto-discover ../../charts

# 解析器测试（54 项检查）
cd /path/to/MinimalPhigrosRend
./cpp/build/test_parser charts
```

## 架构

模块结构、数据流及渲染管线的详细概述请参阅 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)。

## 性能

典型基准测试结果（无窗口，240 Hz 模拟）：

| 谱面 | 音符数 | 速度 |
|------|--------|------|
| AbsoluTedisoRdeR (RPE) | 1600 | ~450× 实时速度 |
| Rrharil (Official) | 1300 | ~467× 实时速度 |
| ATHAZA (RPE) | 1137 | ~514× 实时速度 |
| Aleph0 (RPE) | 885 | ~606× 实时速度 |
| Radiance (Official) | 667 | ~658× 实时速度 |
| BetterGraphicAnimation | 616 | ~712× 实时速度 |

主要优化手段：二分搜索音符可见性窗口、每判定线预计算 `cos_rot`/`sin_rot`、二分搜索轨道定位、栈分配判定线状态数组。
