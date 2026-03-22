# ScriptPlay DSL

`scriptplay` 是 `phigros_render` 的确定性判定脚本格式。

它适合放在完美 autoplay 和手打之间：

- 按过滤条件强制指定音符判定
- 用百分比或毫秒控制 Hold 提前松手
- 在实时播放和 `--score-only` 中复用同一套脚本

## 运行方式

```bash
./cpp/build/phigros_render charts/MyChart/IN.json \
  --score-only \
  --mode scriptplay \
  --scriptplay docs/scriptplay_template.json
```

也可以写进配置：

```json
{
  "gameplay": {
    "mode": "scriptplay",
    "judge_script": "docs/scriptplay_template.json"
  }
}
```

## 基本结构

```json
{
  "version": 1,
  "meta": {
    "name": "mixed test",
    "index_mode": "playable",
    "require_playable_notes": 1600
  },
  "entries": [
    {
      "filter": { "kind": "hold" },
      "judge": { "grade": "GOOD", "dt_ms": 60 },
      "hold": { "percent": 1.0 }
    },
    {
      "filter": { "noteIndexes": [0, 1, 2] },
      "judge": { "grade": "MISS" }
    }
  ]
}
```

## 规则

- `entries` 按顺序应用，后面的匹配项会覆盖前面的字段。
- 没有匹配到的可玩音符默认按 autoplay 方式处理：`PERFECT` + `0ms`。
- `judge.grade` 对 Tap / Drag / Flick 表示最终判定，对 Hold 表示头判等级。
- Hold 最终是否 MISS 仍由引擎规则决定：
  `hold.percent` 或 `hold.ms` 低于 `hold_tail_tol` 时会在提前松手后 MISS。
- `judge.grade = "MISS"` 表示这个音符不触发命中。

## 过滤器

`filter` 支持：

- `startNoteIndex`, `endNoteIndex`
- `noteIndexes`
- `noteIds`
- `lineIds`
- `kind` 或 `kinds`
- `above`
- `fake`
- `mh`
- `playable`

`index_mode` 控制 `noteIndexes` 的计数空间：

- `playable`：只计算非 fake 音符
- `all`：计算全部音符

## 数值形式

`dt_ms`、`hold.percent`、`hold.ms` 支持三种写法：

- 固定值：`60`
- 范围：`{ "min": -40, "max": 40 }`
- 确定性序列：
  `{ "values": [-20, 0, 20], "weights": [1, 4, 1] }`

范围会在匹配到的音符上均匀铺开；序列会按确定性方式循环，不依赖随机数。

## 兼容别名

旧字段也能继续解析：

- `grade`, `dt_ms`, `holdPercent`, `holdMs`
- 顶层 `kind`, `startNoteIndex`, `endNoteIndex`

可直接修改的示例见 [scriptplay_template.json](scriptplay_template.json)。
