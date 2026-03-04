# Python vs C++/Web Alignment Notes

Checked against `phic_renderer` (Python) and `phic_port` + `phic_web`.

## Aligned now

- Judge windows and weights are consistent (`PERFECT=0.045`, `GOOD=0.090`, `BAD=0.150`, weights `1.0/0.6/0/0`).
- Internal note-kind IDs are consistent (`tap=1, drag=2, hold=3, flick=4`).
- JSON type parsing is format-aware:
  - Official-like: `1 tap, 2 drag, 3 hold, 4 flick`
  - RPE: `1 tap, 2 hold, 3 flick, 4 drag` -> remapped to internal IDs
- Parser timing semantics improved:
  - Official note times use per-line BPM unit conversion (`1.875 / bpm`)
  - RPE beat-time arrays use BPM map + `bpmfactor`
- C API ABI v5 adds parity payloads:
  - `phic_engine_step_v2` / `phic_engine_step_auto_v2`
  - judge events now include `note_kind`
  - frame commands include `t_hit_sec` and `hold_end_sec`
- Cross-implementation parity oracle test added:
  - `phic_parity_oracle` (`tests/oracle_compare.py`) compares Python parser+mods outputs vs C++ core output dump.
- C++ mod surface expanded:
  - `full_blue` (non-hold to tap), lane `scale`, and `compress_zip` duplication support.
  - `attach` subset (lane/time offsets, side control, filter).
  - `fade` subset (time + constant alpha with filter).
  - `note_rules`/`note_overrides` subset (`kind`/`speed_mul`/`alpha`/`side`, with filter support).
  - stutter alpha-decay parity (`alpha_decay` / `opacity_decay`).

## Current gaps

- Python parser coverage is still broader (Official/RPE/PEC details, richer line/event decoding).
- C++ parser still simplifies many line/event tracks (especially RPE/PEC line dynamics).
- Python mod pipeline still supports a wider mod/filter surface (x/y/size/tint-focused transforms, line rules, and advanced variants).
- Web runtime keeps a legacy fallback path for old ABI payloads.

## Suggested follow-ups

- Port Python RPE timing/BPM conversion logic into C++ parser.
- Extend C++ mod coverage to match Python mod modules.
- Expand parser/event parity to include full line tracks and richer per-note metadata where behavior depends on them.

---

# Python 与 C++/Web 对齐说明

以下内容与 `phic_renderer`（Python）及 `phic_port` + `phic_web` 对照核查。

## 已对齐项

- 判定窗口与权重已统一（`PERFECT=0.045`、`GOOD=0.090`、`BAD=0.150`，权重 `1.0/0.6/0/0`）。
- 内部 note kind ID 已统一（`tap=1, drag=2, hold=3, flick=4`）。
- JSON 类型解析支持格式感知：
  - Official 格式：`1 tap, 2 drag, 3 hold, 4 flick`
  - RPE 格式：`1 tap, 2 hold, 3 flick, 4 drag` → 重新映射至内部 ID
- 解析器时序语义已改进：
  - Official note 时间使用逐线 BPM 单位换算（`1.875 / bpm`）
  - RPE beat 时间数组使用 BPM 图 + `bpmfactor`
- C API ABI v5 新增对齐载荷：
  - `phic_engine_step_v2` / `phic_engine_step_auto_v2`
  - 判定事件现在包含 `note_kind`
  - 帧命令包含 `t_hit_sec` 和 `hold_end_sec`
- 新增跨实现对齐验证器测试：
  - `phic_parity_oracle`（`tests/oracle_compare.py`）对比 Python 解析器+Mod 输出与 C++ 核心输出 dump。
- C++ Mod 层已扩展：
  - `full_blue`（非 Hold 转 Tap）、lane `scale`、`compress_zip` 重复支持。
  - `attach` 子集（车道/时间偏移、side 控制、filter）。
  - `fade` 子集（时间 + 常量 alpha，含 filter）。
  - `note_rules`/`note_overrides` 子集（`kind`/`speed_mul`/`alpha`/`side`，含 filter 支持）。
  - stutter alpha-decay 对齐（`alpha_decay` / `opacity_decay`）。

## 当前差距

- Python 解析器覆盖范围仍更广（Official/RPE/PEC 细节、更丰富的线/事件解码）。
- C++ 解析器对许多线/事件轨道仍有简化（尤其 RPE/PEC 线动态）。
- Python Mod 流水线支持更广的 Mod/Filter 层（以 x/y/size/tint 为核心的变换、line rules、高级变体）。
- Web 运行时保留了旧版 ABI 载荷的兼容回退路径。

## 建议后续工作

- 将 Python RPE 时序/BPM 换算逻辑移植到 C++ 解析器。
- 扩展 C++ Mod 覆盖，与 Python Mod 模块对齐。
- 将解析器/事件对齐扩展到完整线轨道及更丰富的逐 note 元数据（行为依赖这些数据时）。
