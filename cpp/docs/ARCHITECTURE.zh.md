# 架构概览

> 🌐 [English](ARCHITECTURE.md)

本页记录当前 C++ 代码库的模块布局与运行时流程。

面向使用者的工作流请先看 [../../docs/CPP_RENDERER.zh.md](../../docs/CPP_RENDERER.zh.md)。如果需要子系统细节，再继续看本目录下的专门页面。

## 目录结构

```text
cpp/
├── CMakeLists.txt
├── include/phigros/
│   ├── api/      供 Python 绑定使用的原生 API 面
│   ├── app/      CLI、窗口、输入、平台集成
│   ├── chart/    加载器、解析器、编译器、PHBC I/O
│   ├── config/   RenderConfig 加载 / 保存 / 默认值
│   ├── core/     核心类型、日志、mods
│   ├── engine/   判定、自动游玩、Hold 逻辑、运动学、可见性
│   ├── hud/      HUD 状态
│   ├── io/       音频、回放、资源包、视频编码器
│   ├── math/     缓动、轨道、数学工具
│   └── render/   帧快照、渲染器、执行器、目标纹理
├── src/
│   ├── api/      PreparedChart / 自动游玩 / PHBC 实现
│   ├── app/      原生渲染器可执行入口
│   ├── chart/    解析器 / 加载器 / 编译器实现
│   ├── python/   Python 扩展模块胶水层
│   ├── vendor/   基于 vendor 的翻译单元
│   └── main.cpp  无头 / 原生 core 入口
├── tests/        原生测试与基准入口
├── vendor/       miniz、miniaudio、stb
├── scripts/      构建辅助与 launcher 工具
├── shaders/      bgfx 着色器源码
├── mods/         内置 mod 示例
├── web/          WASM shell 资源
├── android/      Android 包装工程
└── ios/          iOS 包装工程
```

## 目标图

`cpp/CMakeLists.txt` 中的主要目标：

- `phigros_core_lib`：核心谱面 / 解析 / 编译库
- `phigros_core`：构建在 `phigros_core_lib` 上的无头 / 原生 CLI 目标
- `phigros_python_api_lib`：供 Python 扩展使用的原生 API 层
- `chart_scanner`：谱面发现工具
- `test_easing`、`test_engine`、`test_parser`、`test_logger`、`test_zip_extract`、`verify_chart`、`bench`
- `phigros_render`：启用 render-app 目标时构建的渲染器 / 播放器应用

Vendor 支撑库包括 `vendor_stb`、`vendor_miniz` 与 `vendor_miniaudio`。

## 运行时数据流

```text
CLI 参数 / Python 调用 / script 条目
        │
        ▼
RenderConfig + 路径解析
        │
        ▼
chart::load / parse / read_phbc
        │
        ▼
ChartData 或 CompiledChartData
        │
        ▼
PreparedChart / 运行时状态
        │
        ├── engine::SimulatePlayer
        ├── engine::ManualJudge
        ├── engine::Judge
        └── hold / visibility / effects 辅助
                │
                ▼
render::build_frame()
                │
                ▼
FrameSnapshot { lines, notes, hud }
                │
                ▼
SDL/bgfx 渲染路径或 Python 消费端
```

## 模块职责

- `core/`：`Note`、`Line`、`ChartData` 与 note-state 等规范运行时结构。
- `chart/`：格式解析、谱面发现、资源解析、编译谱面转换、PHBC 读写。
- `math/`：供解析器、运动学、编译器、渲染共享的缓动与分段轨道求值。
- `engine/`：时间域的玩法模拟与判定线 / 音符求值辅助。
- `render/`：CPU 侧帧快照构建与后端特定绘制执行。
- `config/`：JSON 到 `RenderConfig` 的转换、默认值、截断与往返序列化。
- `api/`：供 Python 绑定消费的稳定原生辅助层。
- `app/`：可执行集成层、输入循环、CLI 与平台运行时接线。
- `io/`：音频播放、回放持久化、respack 处理与视频输出。

## 内部文档地图

- [INTERFACES.zh.md](INTERFACES.zh.md)
- [DATA_STRUCTURES.zh.md](DATA_STRUCTURES.zh.md)
- [MATH.zh.md](MATH.zh.md)
- [FORMAT.zh.md](FORMAT.zh.md)
- [KINEMATICS.zh.md](KINEMATICS.zh.md)
- [RENDER.zh.md](RENDER.zh.md)
- [CONFIG.zh.md](CONFIG.zh.md)
- [BUILD_AND_TEST.zh.md](BUILD_AND_TEST.zh.md)
- [CHART_LOADER.zh.md](CHART_LOADER.zh.md)
- [DEBUG_FLAGS.zh.md](DEBUG_FLAGS.zh.md)
