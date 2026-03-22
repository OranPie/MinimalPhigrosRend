# 配置使用说明

> 🌐 [English](CONFIG_USAGE.md)

本页说明当前仓库中的配置是如何使用的。

它覆盖当前仍在维护的 C++ 渲染器与 `phigros_cpp` 求值工作流，不再把历史上的独立 Python 渲染器路径当作当前主路径来描述。

## 哪些入口会使用配置

- `phigros_render`：通过 `--config` 加载运行时渲染 / 播放配置
- `phigros_cpp`：逐帧求值与自动游玩辅助函数使用的配置对象
- ChartScript：在同一套渲染配置模型上叠加每个条目 / 片段的覆盖配置

按字段展开的内部参考见 [../cpp/docs/CONFIG.zh.md](../cpp/docs/CONFIG.zh.md)。

## 建议使用的配置文件

- `config/config.jsonc`：原生渲染器共享示例配置
- `config/config.json`：旧版纯 JSON 示例
- `config/simulateplay_test.json`：偏向 simulateplay 的示例

如果你希望写带注释的示例，推荐使用带 `//` 行注释的 JSON。当前加载行为以 `RenderConfig` 实现为准，接受哪些字段与默认值都应以代码为准。

## 优先级

从高到低：

1. CLI 参数，例如 `--width`、`--height`、`--approach`、`--chart-speed`
2. `--config <path>` 文件内容
3. `RenderConfig` 内建默认值

ChartScript 会在同一套渲染配置模型上，为每个条目或片段继续叠加覆盖。

## 渲染器工作流

示例：

```bash
./cpp/build/phigros_render charts/MyChart/IN.json --config config/config.jsonc --width 1920 --height 1080 --approach 2.6
```

常见分工：

- 把稳定的视觉默认值放在配置文件里
- 临时试验性的参数放在 CLI 里
- 播放列表特定视觉放在 ChartScript 覆盖配置里

CLI 用法见 [CPP_RENDERER.zh.md](CPP_RENDERER.zh.md)，字段内部参考见 [../cpp/docs/CONFIG.zh.md](../cpp/docs/CONFIG.zh.md)。

## Python 工作流

`phigros_cpp` 会把同一套配置模型暴露为 Python 侧对象。

常见写法：

```python
import phigros_cpp as pc

cfg = pc.load_config("config/config.jsonc")
chart = pc.load_chart("charts/MyChart/IN.json")
frame = chart.build_frame(12.5, config=cfg)
```

或者：

```python
cfg = pc.config_from_dict({
    "window": {"w": 1920, "h": 1080},
    "render": {"approach": 2.8, "note_outline": True},
})
```

Python API 入口见 [PYTHON_BINDINGS.zh.md](PYTHON_BINDINGS.zh.md)。

## 应该看哪份文档

- 想知道 CLI 怎么传配置：看 [CPP_RENDERER.zh.md](CPP_RENDERER.zh.md)
- 想知道字段默认值、截断规则、嵌套结构或序列化行为：看 [../cpp/docs/CONFIG.zh.md](../cpp/docs/CONFIG.zh.md)
- 想看 ChartScript 覆盖示例：看 [CHARTSCRIPT.zh.md](CHARTSCRIPT.zh.md)
- 想看完整文档地图：看 [INDEX.zh.md](INDEX.zh.md)
