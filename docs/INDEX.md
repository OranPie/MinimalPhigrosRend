# Documentation Index

> 🌐 [中文](INDEX.zh.md)

## By Audience

### Using the renderer

- [CPP_RENDERER.md](CPP_RENDERER.md): build, run, CLI, replay, recording, benchmark workflow
- [CONFIG_USAGE.md](CONFIG_USAGE.md): how config files, CLI overrides, and ChartScript overrides fit together
- [CHARTSCRIPT.md](CHARTSCRIPT.md): playlist/script DSL
- [SCRIPTPLAY.md](SCRIPTPLAY.md): deterministic note-judgment scripting for autoplay-like runs

### Using Python bindings

- [PYTHON_BINDINGS.md](PYTHON_BINDINGS.md): install/build/import and Python API entrypoints
- [CONFIG_USAGE.md](CONFIG_USAGE.md): shared config model from Python

### Working on the C++ codebase

- [../cpp/README.md](../cpp/README.md): C++ directory quickstart
- [../cpp/docs/ARCHITECTURE.md](../cpp/docs/ARCHITECTURE.md): module tree, target graph, data flow
- [../cpp/docs/INTERFACES.md](../cpp/docs/INTERFACES.md): executable/API boundaries
- [../cpp/docs/DATA_STRUCTURES.md](../cpp/docs/DATA_STRUCTURES.md): core structs and invariants
- [../cpp/docs/MATH.md](../cpp/docs/MATH.md): easing, tracks, sampled/compiled math model
- [../cpp/docs/FORMAT.md](../cpp/docs/FORMAT.md): source chart formats, packaging, PHBC
- [../cpp/docs/KINEMATICS.md](../cpp/docs/KINEMATICS.md): line/note evaluation and coordinate rules
- [../cpp/docs/RENDER.md](../cpp/docs/RENDER.md): frame snapshots, layers, backends
- [../cpp/docs/CONFIG.md](../cpp/docs/CONFIG.md): internal config semantics and defaults
- [../cpp/docs/BUILD_AND_TEST.md](../cpp/docs/BUILD_AND_TEST.md): CMake targets, platforms, tests, benchmarks

## Repository Map

```text
root docs/        User workflows and navigation
cpp/docs/         Internal C++ module documentation
config/           Example configs
cpp/tests/        Native tests and benchmark entrypoints
scripts/          Local helper tools
cpp/scripts/      C++-specific helper scripts
```

## Reading Order

1. Start from the root README or [CPP_RENDERER.md](CPP_RENDERER.md) if you are trying to run something.
2. Move to [CONFIG_USAGE.md](CONFIG_USAGE.md) or [PYTHON_BINDINGS.md](PYTHON_BINDINGS.md) based on your entry surface.
3. Use `cpp/docs/` only when you need implementation detail, subsystem ownership, or internal behavior.
