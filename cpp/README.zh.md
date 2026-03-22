# MinimalPhigrosRend — C++ 渲染器

> 🌐 [English](README.md)

这个目录包含原生 C++ 实现：解析 / 核心库目标、渲染器 / 播放器应用、测试、平台包装工程、着色器，以及内部子系统文档。

## 目录结构

```text
cpp/
├── CMakeLists.txt
├── README.md / README.zh.md
├── cmake/                移动端 / 平台辅助 CMake 文件
├── include/phigros/
│   ├── api/              暴露给 Python 绑定的原生 API
│   ├── app/              CLI、窗口、输入、游戏循环集成
│   ├── chart/            谱面加载、解析、编译、PHBC I/O
│   ├── config/           RenderConfig 加载 / 保存与默认值
│   ├── core/             核心类型、日志、mods
│   ├── engine/           判定、自动游玩、Hold 逻辑、运动学、可见性
│   ├── hud/              HUD 状态
│   ├── io/               音频、回放、资源包、视频编码器
│   ├── math/             缓动、轨道、数学工具
│   └── render/           帧构建与绘制后端
├── src/
│   ├── app/              原生渲染器应用入口
│   ├── api/              供绑定使用的 C++ API 实现
│   ├── chart/            解析器 / 加载器 / 编译器实现
│   ├── python/           Python 扩展模块胶水层
│   ├── vendor/           Vendor 包装翻译单元
│   └── main.cpp          无头 / 原生 core 入口
├── docs/                 C++ 内部文档集合
├── tests/                原生测试与基准
├── scripts/              构建与辅助脚本
├── shaders/              bgfx 着色器源码
├── mods/                 内置 mod JSON 示例
├── vendor/               内嵌第三方库
├── web/                  WASM shell 资源
├── android/              Android 包装工程
└── ios/                  iOS 包装工程
```

## 构建

桌面端构建：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DUSE_BGFX=OFF
cmake --build build --parallel
```

常见产物：

- `build/phigros_render`
- `build/phigros_core`
- `build/chart_scanner`
- `build/test_engine` 等测试可执行文件

## 文档入口

- 渲染器 CLI 使用：[../docs/CPP_RENDERER.zh.md](../docs/CPP_RENDERER.zh.md)
- Python 绑定使用：[../docs/PYTHON_BINDINGS.zh.md](../docs/PYTHON_BINDINGS.zh.md)
- 内部架构：[docs/ARCHITECTURE.zh.md](docs/ARCHITECTURE.zh.md)
- 接口：[docs/INTERFACES.zh.md](docs/INTERFACES.zh.md)
- 数据结构：[docs/DATA_STRUCTURES.zh.md](docs/DATA_STRUCTURES.zh.md)
- 数学：[docs/MATH.zh.md](docs/MATH.zh.md)
- 格式内部说明：[docs/FORMAT.zh.md](docs/FORMAT.zh.md)
- 运动学：[docs/KINEMATICS.zh.md](docs/KINEMATICS.zh.md)
- 渲染管线：[docs/RENDER.zh.md](docs/RENDER.zh.md)
- 配置内部说明：[docs/CONFIG.zh.md](docs/CONFIG.zh.md)
- 构建与测试：[docs/BUILD_AND_TEST.zh.md](docs/BUILD_AND_TEST.zh.md)

## 说明

- 根目录 `docs/` 负责面向使用者的工作流文档。
- `cpp/docs/` 是 C++ 内部架构 / 模块参考的规范位置。
