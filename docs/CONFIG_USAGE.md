# Configuration Usage

> 🌐 [中文](CONFIG_USAGE.zh.md)

This page explains how configuration is used in the current repository.

It covers the active C++ renderer and `phigros_cpp` evaluation workflows. It does not document the historical standalone Python renderer path.

## What Uses Config

- `phigros_render`: runtime renderer/player config loaded from `--config`
- `phigros_cpp`: config objects used for frame evaluation and autoplay helpers
- ChartScript: per-item and preset config overrides layered onto the same render config model

For the field-by-field internal reference, see [../cpp/docs/CONFIG.md](../cpp/docs/CONFIG.md).

## Recommended Files

- `config/config.jsonc`: shared example config for the native renderer
- `config/config.json`: legacy plain JSON example
- `config/simulateplay_test.json`: simulateplay-oriented example

Use JSON with `//` line comments if you want commented examples. The current loader is implemented in `RenderConfig` and should be treated as the source of truth for accepted fields and defaults.

## Precedence

From highest precedence to lowest:

1. CLI flags such as `--width`, `--height`, `--approach`, `--chart-speed`
2. `--config <path>` file contents
3. `RenderConfig` built-in defaults

ChartScript applies config overrides on top of the same render config model for each item or segment.

## Renderer Workflow

Example:

```bash
./cpp/build/phigros_render charts/MyChart/IN.json --config config/config.jsonc --width 1920 --height 1080 --approach 2.6
```

Typical split:

- keep stable visual defaults in the config file
- use CLI flags for one-off local experiments
- use ChartScript overrides for playlist-specific visuals

See [CPP_RENDERER.md](CPP_RENDERER.md) for CLI usage and [../cpp/docs/CONFIG.md](../cpp/docs/CONFIG.md) for the internal field reference.

## Python Workflow

`phigros_cpp` exposes the same config model as a Python-facing object.

Typical patterns:

```python
import phigros_cpp as pc

cfg = pc.load_config("config/config.jsonc")
chart = pc.load_chart("charts/MyChart/IN.json")
frame = chart.build_frame(12.5, config=cfg)
```

or:

```python
cfg = pc.config_from_dict({
    "window": {"w": 1920, "h": 1080},
    "render": {"approach": 2.8, "note_outline": True},
})
```

See [PYTHON_BINDINGS.md](PYTHON_BINDINGS.md) for the Python API surface.

## When To Read Which Doc

- Want to know how to pass config on the CLI: [CPP_RENDERER.md](CPP_RENDERER.md)
- Want field defaults, clamps, nested sections, or serialization rules: [../cpp/docs/CONFIG.md](../cpp/docs/CONFIG.md)
- Want ChartScript override examples: [CHARTSCRIPT.md](CHARTSCRIPT.md)
- Want the full doc map: [INDEX.md](INDEX.md)
