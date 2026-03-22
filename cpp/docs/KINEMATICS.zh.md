# 运动学

> 🌐 [English](KINEMATICS.md)

运动学层负责把判定线状态、滚动状态与音符局部放置转换为世界空间位置。

## 核心类型与函数

定义于 `engine/kinematics.hpp`：

- `LineState`
- `eval_line_state()`
- `Vec2`
- `note_world_pos()`
- `note_world_pos_cs()`

## `LineState`

一条判定线在时间 `t` 的状态会打包为：

- 判定线位置：`x`、`y`
- 判定线旋转：`rot`
- alpha 值：`alpha01`、`alpha_raw`
- 累计滚动：`scroll`
- 缓存的三角函数：`cos_rot`、`sin_rot`

`eval_line_state()` 还会应用来自 `RenderConfig` 的可选强制判定线 alpha 覆盖。

## 坐标模型

运行时使用的是判定线局部基：

- 切线方向跟随判定线旋转
- 法线方向与判定线垂直
- `x_local_px` 沿切线方向移动
- 滚动距离沿法线方向移动
- `above` 决定法线方向的符号

## 音符位置求值

`note_world_pos()` 与 `note_world_pos_cs()` 会基于以下输入求出音符世界坐标：

- 当前判定线状态
- 音符局部放置
- 当前滚动值
- 音符命中时或 Hold 结束时的目标滚动值
- 流速倍率与 speed-mul 行为开关

`note_world_pos_cs()` 是热路径版本，直接接收预计算的 cosine / sine，从而避免重复三角函数调用。

## Hold 与速度规则

关键运行时规则：

- 正在按住时，Hold 头部可以选择钉在判定线位置
- Hold 尾部使用 `scroll_end`
- 非 Hold 的进场距离可以选择是否受 `speed_mul` 影响
- Hold 尾部路径始终会考虑 `speed_mul`

## 与渲染相邻的控制事件

运动学给出的是基础位置。渲染阶段还会应用额外控制事件调整，例如：

- alpha 调制
- x 方向位置倍率
- 额外 y 偏移
- 尺寸缩放
- skew

这种分层是有意的：运动学负责基础几何，渲染负责展示修饰。

## 相关文档

- [MATH.zh.md](MATH.zh.md)
- [RENDER.zh.md](RENDER.zh.md)
- [DATA_STRUCTURES.zh.md](DATA_STRUCTURES.zh.md)
