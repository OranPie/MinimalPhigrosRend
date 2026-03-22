# Format

> 🌐 [中文](FORMAT.zh.md)

This page documents the chart and compiled-format model used by the C++ core.

## Source Chart Formats

Supported source formats are represented under `include/phigros/chart/` and implemented in `src/chart/`:

- `official.hpp` / `official.cpp`: Phigros official JSON
- `rpe.hpp` / `rpe.cpp`: RPE JSON
- `pec.hpp` / `pec.cpp`: PEC format

All successful parse paths end in the same canonical runtime representation: `ChartData`.

## Discovery and Packaging Forms

The loader layer supports more than raw file parsing:

- folder charts with multiple difficulty files and auto-detected assets
- standalone chart files with sibling music/illustration lookup
- zip-based chart packages via extraction helpers
- `.phbc` compiled charts for fast reload paths

The discovery-facing structures are `ChartAssets` and `ChartEntry`.

For the operational scanning API, see [CHART_LOADER.md](CHART_LOADER.md).

## Compiled Format

`CompiledChartData` is the in-memory compiled representation used for PHBC round trips.

Key characteristics:

- stores sampled line tracks in flat float arrays
- keeps plain note records with baked visibility timing
- can be converted back to `ChartData` without changing downstream engine/render code
- separates source-format parsing from playback-time evaluation cost

## PHBC

PHBC is the compiled chart container used by `read_phbc()` and `write_phbc()`.

In this repository:

- v1 is the basic compiled-chart container
- v2 adds compressed and/or encrypted payload workflows
- supported compression paths include zlib and optional LZMA
- supported encryption paths include OpenSSL-backed modes plus XOR fallback

Write options are carried by `PhbcWriteOptions` in the chart layer and are also exposed through the binding/API path.

## Format Flow

```text
source file / folder / zip / phbc
        │
        ▼
chart loader / parser / phbc reader
        │
        ▼
ChartData or CompiledChartData
        │
        ├── compile to PHBC
        └── to_chart_data() for runtime engine/render use
```

## Related Docs

- [CHART_LOADER.md](CHART_LOADER.md)
- [DATA_STRUCTURES.md](DATA_STRUCTURES.md)
- [MATH.md](MATH.md)
- [../../docs/CPP_RENDERER.md](../../docs/CPP_RENDERER.md)
