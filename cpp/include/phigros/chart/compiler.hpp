#pragma once
#include "phigros/core/types.hpp"
#include "phigros/chart/compiled_chart.hpp"

namespace phigros::chart {

// Sample every line track in `src` at `sample_rate` Hz across the chart's
// full time range and return a CompiledChartData ready for binary export.
//
// Requirements:
//   - src.finalize() must have been called (chart_end_t / playable_count set)
//   - engine::precompute_t_enter() must have been called (note.t_enter set)
//   - W, H are the canvas dimensions used during precompute_t_enter
//
// Default sample_rate is 240 Hz. Raise it for smoother fast easings; lower it
// to reduce file size. The output's t_enter is baked, so the caller should set
// ChartData::is_compiled = true after to_chart_data() (handled automatically).
CompiledChartData compile_chart(const ChartData& src,
                                float sample_rate = 240.0f);

} // namespace phigros::chart
