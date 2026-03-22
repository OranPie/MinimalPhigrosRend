# 文档索引

> 🌐 [English](INDEX.md)

## 按读者分类

### 渲染器使用者

- [CPP_RENDERER.zh.md](CPP_RENDERER.zh.md)：构建、运行、CLI、回放、录制、基准测试
- [CONFIG_USAGE.zh.md](CONFIG_USAGE.zh.md)：配置文件、CLI 覆盖与 ChartScript 覆盖之间的关系
- [CHARTSCRIPT.zh.md](CHARTSCRIPT.zh.md)：播放列表 / 脚本 DSL

### Python 绑定使用者

- [PYTHON_BINDINGS.zh.md](PYTHON_BINDINGS.zh.md)：安装 / 构建 / 导入与 Python API 入口
- [CONFIG_USAGE.zh.md](CONFIG_USAGE.zh.md)：Python 侧共享配置模型

### C++ 代码库维护者

- [../cpp/README.zh.md](../cpp/README.zh.md)：C++ 目录快速入口
- [../cpp/docs/ARCHITECTURE.zh.md](../cpp/docs/ARCHITECTURE.zh.md)：模块树、目标图、数据流
- [../cpp/docs/INTERFACES.zh.md](../cpp/docs/INTERFACES.zh.md)：可执行入口与 API 边界
- [../cpp/docs/DATA_STRUCTURES.zh.md](../cpp/docs/DATA_STRUCTURES.zh.md)：核心结构体与不变量
- [../cpp/docs/MATH.zh.md](../cpp/docs/MATH.zh.md)：缓动、轨道、采样 / 编译数学模型
- [../cpp/docs/FORMAT.zh.md](../cpp/docs/FORMAT.zh.md)：源谱面格式、打包方式、PHBC
- [../cpp/docs/KINEMATICS.zh.md](../cpp/docs/KINEMATICS.zh.md)：判定线 / 音符求值与坐标规则
- [../cpp/docs/RENDER.zh.md](../cpp/docs/RENDER.zh.md)：帧快照、图层、后端
- [../cpp/docs/CONFIG.zh.md](../cpp/docs/CONFIG.zh.md)：配置内部语义与默认值
- [../cpp/docs/BUILD_AND_TEST.zh.md](../cpp/docs/BUILD_AND_TEST.zh.md)：CMake 目标、平台、测试、基准

## 仓库地图

```text
root docs/        面向使用者的工作流文档与导航
cpp/docs/         C++ 内部模块文档
config/           示例配置
cpp/tests/        原生测试与基准入口
scripts/          根目录本地辅助工具
cpp/scripts/      C++ 专用辅助脚本
```

## 建议阅读顺序

1. 如果你是想跑起来，先看根 README 或 [CPP_RENDERER.zh.md](CPP_RENDERER.zh.md)。
2. 再根据入口选择 [CONFIG_USAGE.zh.md](CONFIG_USAGE.zh.md) 或 [PYTHON_BINDINGS.zh.md](PYTHON_BINDINGS.zh.md)。
3. 只有在需要实现细节、模块责任划分或内部行为时，再进入 `cpp/docs/`。
