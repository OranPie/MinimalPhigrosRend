# 渲染

> 🌐 [English](RENDER.md)

渲染层分为两部分：

- `render/renderer.hpp` 中的 CPU 侧帧构建
- `render/` 目录下特定后端的绘制辅助与执行器

## 帧构建

`build_frame()` 接收：

- 时间 `t`
- `ChartData`
- 每音符 `NoteState` 数组
- `engine::Judge`
- `RenderConfig`

它返回一个 `FrameSnapshot`，其中包含求值后的 `LineSnapshot`、`NoteSnapshot` 与 HUD 状态。

## 高层管线

```text
判定线求值
  -> 按 z-order 排序判定线
  -> 过滤音符候选
  -> 求世界坐标
  -> 应用渲染时控制事件修正
  -> 处理音符 alpha 与裁剪
  -> 生成 HUD 快照
  -> 进入后端绘制路径
```

## 快照职责

- `LineSnapshot`：变换、alpha、scroll、颜色、line 文本 / 纹理、cover / z-order 状态
- `NoteSnapshot`：求值后的头 / 尾位置、alpha、颜色、尺寸、Hold 标志、miss / 判定标志
- `FrameSnapshot`：供原生渲染与 Python 侧检查使用的顶层载荷

## 绘制子系统

`include/phigros/render/` 下的主要渲染辅助：

- `background.hpp`
- `line_renderer.hpp`
- `note_renderer.hpp`
- `hold_renderer.hpp`
- `hitfx_renderer.hpp`
- `hud_renderer.hpp`
- `pause_overlay.hpp`
- `result_screen.hpp`
- `trail_renderer.hpp`
- `motion_blur.hpp`
- 以及 `sdl_renderer.hpp`、`sdl_executor.hpp`、`bgfx_renderer.hpp`、`bgfx_executor.hpp` 等执行器 / 后端

## 可见性与 Alpha 行为

重要的渲染时行为包括：

- 面向高密度谱面的可选 `t_enter` 裁剪
- expand 变换后的屏幕空间裁剪
- Hold 同时检查头 / 尾的可见性
- 由音符自身 alpha、控制事件、全局配置、判定线 alpha 模式共同决定最终 alpha
- 提交绘制前先根据 cover / z-order 排序判定线

## 性能说明

代码里已经可见的热路径决策：

- 对常见判定线数量使用栈存储
- 预计算判定线三角函数并在音符间复用
- 对音符快照向量进行自适应 reserve
- 使用编译采样轨道，减少重复分段轨道求值开销

## 相关文档

- [KINEMATICS.zh.md](KINEMATICS.zh.md)
- [DATA_STRUCTURES.zh.md](DATA_STRUCTURES.zh.md)
- [CONFIG.zh.md](CONFIG.zh.md)
