# 构建与测试

> 🌐 [English](BUILD_AND_TEST.md)

本页记录主要构建开关、目标族，以及验证工作流。

## 主要配置开关

来自 `cpp/CMakeLists.txt` 的重要 CMake 选项：

- `BUILD_PYTHON_BINDINGS`
- `BUILD_RENDER_APP`（默认 SDL 原生应用目标）
- `BUILD_LEGACY_CLI`（旧 argv 入口 `phigros_render`）
- `BUILD_MOBILE_BRIDGE`（旧 Kotlin/Swift 原生 UI 桥）
- `USE_SDL3`
- `USE_BGFX`
- `USE_LIBAV`
- `USE_LZMA`
- `USE_ENCRYPTION`
- `USE_SANITIZERS`

这些选项决定哪些运行入口与可选依赖会被编译进来。

## 常见构建路径

SDL 应用构建：

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release -DUSE_BGFX=OFF
cmake --build cpp/build --target phigros_sdl_app --parallel
```

绑定构建：

```bash
cmake -S cpp -B cpp/build_py -DBUILD_PYTHON_BINDINGS=ON -DBUILD_RENDER_APP=OFF -DUSE_BGFX=OFF -DUSE_LIBAV=OFF
cmake --build cpp/build_py --target _core --parallel
```

## 目标族

核心 / 运行时目标：

- `phigros_core_lib`
- `phigros_core`
- `phigros_python_api_lib`
- `phigros_sdl_app`
- 打开 `BUILD_LEGACY_CLI=ON` 时的 `phigros_render`
- 打开 `BUILD_MOBILE_BRIDGE=ON` 时的 `phigros_mobile_bridge`
- `chart_scanner`

测试与基准目标：

- `test_easing`
- `test_engine`
- `test_parser`
- `test_logger`
- `test_zip_extract`
- `verify_chart`
- `bench`
- 二进制存在时可用的 `run-tests` 便利目标

## 平台说明

- 桌面端：启用时默认使用 SDL3，也保留 SDL2 回退路径。
- WebAssembly：通过 web 构建辅助脚本与 Emscripten 环境构建。
- Android/iOS：最小平台包装工程加载原生 SDL 应用。旧业务 UI 桥位于 `BUILD_MOBILE_BRIDGE` 后。
- 可选的编解码 / 压缩 / 加密支持取决于系统包与构建开关。

## 验证工作流

常见原生检查：

```bash
./cpp/build/test_easing
./cpp/build/test_engine
./cpp/build/test_parser charts
```

与文档强相关的检查：

- 对照 `app_args.hpp` 验证 CLI 标志
- 对照 `render_config.hpp` 验证配置字段
- 对照 `cpp/CMakeLists.txt` 验证目标名称
- 对照 `cpp/include/phigros` 与 `cpp/src` 验证模块树

## 文档维护规则

以下内容变化时必须同步更新文档：

- 公开 CLI 标志
- `RenderConfig` 字段或默认值
- 核心谱面 / 运行时结构体
- 谱面或 PHBC 格式行为
- 目标名称或主要构建开关
- 后端 / 平台支持边界
