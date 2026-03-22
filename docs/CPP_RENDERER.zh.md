# C++ 渲染器参考

> 🌐 [English](CPP_RENDERER.md)

`phigros_render` 是当前仓库面向使用者的原生渲染器 / 播放器入口。

本页面向构建与运行工作流。内部子系统细节请看 [../cpp/docs/](../cpp/docs/)。

## 快速构建

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release -DUSE_BGFX=OFF
cmake --build cpp/build --parallel
```

产物：

- `cpp/build/phigros_render`

## 快速运行

```bash
./cpp/build/phigros_render charts/MyChart/IN.json
```

常见任务：

```bash
./cpp/build/phigros_render charts/MyChart/IN.json --score-only
./cpp/build/phigros_render charts/MyChart/IN.json --play
./cpp/build/phigros_render charts/MyChart/IN.json --mode scriptplay --scriptplay docs/scriptplay_template.json
./cpp/build/phigros_render charts/MyChart/IN.json --record out.mp4
./cpp/build/phigros_render charts/MyChart/IN.json --benchmark --benchmark-iterations 20
./cpp/build/phigros_render charts/MyChart/IN.json --config config/config.jsonc
```

## 支持的输入

- `.json`：Official 或 RPE 谱面
- `.pec`：PEC 谱面
- `.phbc`：预编译二进制谱面
- 目录与 zip 发现工作流可通过 `--list-charts` 和 chart loader 辅助函数使用

格式内部说明见 [../cpp/docs/FORMAT.zh.md](../cpp/docs/FORMAT.zh.md) 与 [../cpp/docs/CHART_LOADER.zh.md](../cpp/docs/CHART_LOADER.zh.md)。

## 常见 CLI 区域

播放控制：

- `--mode <autoplay|manual|scriptplay>`
- `--play`
- `--scriptplay <file.json>`
- `--score-only`
- `--duration <sec>`
- `--truncate-at-duration`
- `--audio-offset <ms>`
- `--width <px>` / `--height <px>`
- `--headless`

视觉覆盖：

- `--approach <sec>`
- `--chart-speed <mul>`
- `--expand <factor>`
- `--note-scale-x <mul>` / `--note-scale-y <mul>`
- `--note-alpha <0-1>`
- `--font-size <mul>`
- `--overlay-transparent`
- `--debug-flags <flags>`

回放与录制：

- `--save-replay <file>` / `--play-replay <file>`
- `--record <out.mp4>`
- `--record-preset`、`--record-codec`、`--record-hw`
- `--record-fps`、`--sim-fps`
- `--record-resolution`、`--record-capture-resolution`
- `--record-start`、`--record-end`

编译与脚本工作流：

- `--compile <out.phbc>`
- `--sample-rate <Hz>`
- `--compress [zlib|lzma]`
- `--encrypt [aes-gcm|aes-cbc|chacha20|xor]`
- `--password <passphrase>`
- `--script <file.chartscript.json>`
- `--scriptplay <file.json>`
- `--mod <file.mod.json>`

工具与日志：

- `--info`、`--version`、`--help`
- `--list-charts <dir>`
- `--profile`、`--record-profile`
- `--log-level`、`--log-filter`、`--log-file`
- `--trace`、`--verbose`、`--quiet`

## 配置

通过 `--config <path>` 使用 JSON / JSONC 风格配置文件：

```bash
./cpp/build/phigros_render charts/MyChart/IN.json --config config/config.jsonc
```

建议继续阅读：

- 共享使用说明：[CONFIG_USAGE.zh.md](CONFIG_USAGE.zh.md)
- 内部字段参考：[../cpp/docs/CONFIG.zh.md](../cpp/docs/CONFIG.zh.md)
- 调试覆盖层：[../cpp/docs/DEBUG_FLAGS.zh.md](../cpp/docs/DEBUG_FLAGS.zh.md)

## 相关文档

- C++ 快速入口：[../cpp/README.zh.md](../cpp/README.zh.md)
- Python 绑定：[PYTHON_BINDINGS.zh.md](PYTHON_BINDINGS.zh.md)
- ChartScript：[CHARTSCRIPT.zh.md](CHARTSCRIPT.zh.md)
- ScriptPlay DSL：[SCRIPTPLAY.zh.md](SCRIPTPLAY.zh.md)
- 内部渲染管线：[../cpp/docs/RENDER.zh.md](../cpp/docs/RENDER.zh.md)
- 内部架构：[../cpp/docs/ARCHITECTURE.zh.md](../cpp/docs/ARCHITECTURE.zh.md)
