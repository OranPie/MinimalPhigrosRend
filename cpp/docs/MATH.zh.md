# 数学

> 🌐 [English](MATH.md)

数学层提供了解析、编译、运动学与渲染共用的可复用求值器。

## 缓动

`math/easing.hpp` 包含面向 RPE 事件求值的缓动表。

它用于：

- 解析带 easing 索引的 RPE 事件
- 在谱面编译阶段对轨道采样
- 运行时对分段缓动曲线求值

## 分段轨道

`math/tracks.hpp` 是核心时间域求值层。

关键概念：

- 常量段与线性段
- 面向 RPE 风格过渡的缓动段
- 用于累计滚动距离的 `IntegralTrack`
- 基于 seek / 二分搜索的分段曲线求值

这一层负责把谱面事件列表转成可调用的时间函数。

## 数学工具

`math/util.hpp` 存放到处复用的底层辅助：

- clamp 与数值辅助
- 轻量 RGB / 颜色结构
- core、engine、render 共用的小型数学工具

## 节拍 / 时间与采样辅助

`chart/` 目录中的一些结构也属于数学链路的一部分：

- `bpm_map.hpp`：基于 BPM 分段的拍数到秒数转换
- `sampled_track.hpp`：均匀采样浮点数组求值器
- `compiled_chart.hpp`：采样轨道存储，以及重新包装为运行时 lambda

## 运行时关系

流程如下：

```text
源事件
  -> 分段轨道 / BPM 映射
  -> 可选的编译期采样
  -> 运行时 TrackFn 求值
  -> 运动学与渲染快照生成
```

## 交叉阅读

- 运动与坐标规则：[KINEMATICS.zh.md](KINEMATICS.zh.md)
- 事件来源的格式层：[FORMAT.zh.md](FORMAT.zh.md)
- 承载这些求值器的核心结构：[DATA_STRUCTURES.zh.md](DATA_STRUCTURES.zh.md)
