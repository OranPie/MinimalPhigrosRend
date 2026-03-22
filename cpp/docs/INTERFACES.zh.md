# 接口

> 🌐 [English](INTERFACES.md)

本页用于区分面向使用者、面向绑定，以及仅内部使用的接口面。

## 面向使用者的入口

- `phigros_render`：原生渲染器 / 播放器 CLI
- `phigros_cpp`：由原生核心构建出的 Python 包
- `chart_scanner`：谱面发现工具二进制

外部使用者应优先依赖这些入口。

## 原生头文件接口

`include/phigros/` 下的主要原生接口：

- `chart/`：谱面加载、格式解析、编译、PHBC 读写
- `config/render_config.hpp`：`RenderConfig` 与配置转换辅助
- `core/types.hpp`：规范运行时结构体
- `engine/`：判定逻辑、自动游玩、可见性、运动学、Hold 逻辑
- `render/renderer.hpp`：CPU 侧 `FrameSnapshot` 构建器
- `api/python_api.hpp`：通过 Python 绑定层暴露的 prepared-chart 与自动游玩辅助

## 可执行入口与模块入口

- `src/app/main.cpp`：渲染器 / 播放器可执行入口
- `src/main.cpp`：无头 / 原生 core 入口
- `src/python/module.cpp`：Python 扩展模块定义
- `src/api/python_api.cpp`：供绑定共享的原生 API 实现
- `include/phigros/app/app_args.hpp`：CLI 选项接口定义

## 绑定边界

Python 包的暴露范围刻意比原生渲染器应用更窄：

- 已暴露：谱面加载、配置加载、逐帧求值、自动游玩、PHBC 工作流
- 未暴露：SDL 窗口层、纹理所有权、后端执行器、app/game-loop 集成

在绑定边界的原生侧，`PreparedChart`、`FrameEvaluator`、自动游玩辅助，以及 PHBC 辅助函数是主要概念。

## 仅内部使用的接口

除非某页明确把它们记为公开契约，否则以下内容应视为实现细节：

- `app/` 下的窗口、输入管理器、平台接线类
- `render/` 中的后端执行器与渲染器专用绘制辅助
- `src/vendor/` 中的 vendor 包装翻译单元
- `android/` 与 `ios/` 下的移动端包装工程

## 相关文档

- [DATA_STRUCTURES.zh.md](DATA_STRUCTURES.zh.md)
- [FORMAT.zh.md](FORMAT.zh.md)
- [RENDER.zh.md](RENDER.zh.md)
- [../../docs/PYTHON_BINDINGS.zh.md](../../docs/PYTHON_BINDINGS.zh.md)
