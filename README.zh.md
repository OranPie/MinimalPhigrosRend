# MinimalPhigrosRend

> 🌐 [English](README.md)

MinimalPhigrosRend 是一个以现代 C++ 核心为中心的 Phigros 谱面渲染与谱面处理工具集。

当前仓库中持续维护的运行入口主要有：

- `phigros_render`：桌面端 / WebAssembly 原生 C++ 渲染器与播放器
- `phigros_cpp`：Python 绑定，提供谱面加载、预处理、逐帧求值、自动游玩模拟与 PHBC 工作流
- 一组辅助脚本，用于本地预览、谱面扫描、ChartScript 播放与 Web UI 导出

## 从哪里开始

- 想构建并运行渲染器：见 [docs/CPP_RENDERER.zh.md](docs/CPP_RENDERER.zh.md)
- 想使用 Python 绑定：见 [docs/PYTHON_BINDINGS.zh.md](docs/PYTHON_BINDINGS.zh.md)
- 想看完整文档索引：见 [docs/INDEX.zh.md](docs/INDEX.zh.md)
- 想看 C++ 内部架构与模块说明：见 [cpp/docs/ARCHITECTURE.zh.md](cpp/docs/ARCHITECTURE.zh.md)

## 仓库结构

```text
MinimalPhigrosRend/
├── README.md / README.zh.md
├── docs/                 面向使用者的文档与导航页
├── config/               示例配置文件
├── assets/               共享资源，例如字体
├── scripts/              根目录辅助脚本与本地工具
├── python/               phigros_cpp 的 Python 包源码
├── cpp/
│   ├── README.md         C++ 渲染器快速开始
│   ├── CMakeLists.txt    原生构建目标定义
│   ├── include/phigros/  按子系统划分的公开 / 原生头文件
│   ├── src/              原生实现
│   ├── docs/             C++ 内部架构与子系统文档
│   ├── tests/            原生测试可执行文件
│   ├── scripts/          平台与构建辅助脚本
│   ├── shaders/          bgfx 着色器源码
│   ├── mods/             内置 mod 示例
│   ├── web/              Web shell 资源
│   ├── android/          Android 包装工程
│   └── ios/              iOS 包装工程
└── respack.zip           默认资源包
```

## 快速开始

构建原生渲染器：

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release -DUSE_BGFX=OFF
cmake --build cpp/build --parallel
```

运行谱面：

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

## 常用文档入口

- 渲染器快速开始：[cpp/README.zh.md](cpp/README.zh.md)
- 渲染器参考：[docs/CPP_RENDERER.zh.md](docs/CPP_RENDERER.zh.md)
- Python 绑定参考：[docs/PYTHON_BINDINGS.zh.md](docs/PYTHON_BINDINGS.zh.md)
- 配置使用说明：[docs/CONFIG_USAGE.zh.md](docs/CONFIG_USAGE.zh.md)
- ChartScript 参考：[docs/CHARTSCRIPT.zh.md](docs/CHARTSCRIPT.zh.md)
- C++ 内部文档索引：[cpp/docs/ARCHITECTURE.zh.md](cpp/docs/ARCHITECTURE.zh.md)

## 说明

- 根目录 `docs/` 面向使用者。
- `cpp/docs/` 面向 C++ 内部模块、数据、数学、格式、渲染与构建说明。
- 如果你希望跑解析器自动发现测试或做本地样例运行，通常需要本地存在 `charts/` 目录。
