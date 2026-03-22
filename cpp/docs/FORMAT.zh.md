# 格式

> 🌐 [English](FORMAT.md)

本页记录 C++ 核心中使用的谱面格式与编译格式模型。

## 源谱面格式

支持的源格式声明在 `include/phigros/chart/`，实现位于 `src/chart/`：

- `official.hpp` / `official.cpp`：Phigros 官方 JSON
- `rpe.hpp` / `rpe.cpp`：RPE JSON
- `pec.hpp` / `pec.cpp`：PEC 格式

所有成功的解析路径最终都会落到同一个规范运行时表示：`ChartData`。

## 发现与打包形式

加载层支持的不只是原始文件解析：

- 带多难度文件与自动资源发现的文件夹谱面
- 通过同级音乐 / 插图自动查找的独立谱面文件
- 通过提取辅助函数支持的 zip 包谱面
- 适合快速重复加载的 `.phbc` 编译谱面

面向发现的结构体是 `ChartAssets` 与 `ChartEntry`。

如果要看扫描 API 的操作层说明，见 [CHART_LOADER.zh.md](CHART_LOADER.zh.md)。

## 编译格式

`CompiledChartData` 是用于 PHBC 往返的内存编译表示。

关键特征：

- 把判定线轨道以平坦浮点数组方式保存
- 保存带可见性时间烘焙结果的普通 note 记录
- 能重新转换为 `ChartData`，无需改动下游 engine/render 代码
- 把源格式解析成本与播放时求值成本分离

## PHBC

PHBC 是 `read_phbc()` 与 `write_phbc()` 使用的编译谱面容器。

在当前仓库中：

- v1 是基础编译谱面容器
- v2 增加压缩和 / 或加密载荷工作流
- 支持的压缩路径包括 zlib 与可选的 LZMA
- 支持的加密路径包括基于 OpenSSL 的模式以及 XOR 回退

写入选项由 chart 层的 `PhbcWriteOptions` 承载，同时也通过绑定 / API 路径暴露。

## 格式流转

```text
源文件 / 文件夹 / zip / phbc
        │
        ▼
chart loader / parser / phbc reader
        │
        ▼
ChartData 或 CompiledChartData
        │
        ├── 编译为 PHBC
        └── 通过 to_chart_data() 进入运行时 engine/render
```

## 相关文档

- [CHART_LOADER.zh.md](CHART_LOADER.zh.md)
- [DATA_STRUCTURES.zh.md](DATA_STRUCTURES.zh.md)
- [MATH.zh.md](MATH.zh.md)
- [../../docs/CPP_RENDERER.zh.md](../../docs/CPP_RENDERER.zh.md)
